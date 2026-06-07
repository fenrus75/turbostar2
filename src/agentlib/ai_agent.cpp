#include "ai_agent.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include "../config_manager.h"
#include "../event_logger.h"
#include "../event_queue.h"
#include "../fs_utils.h"
#include "../project_manager.h"
#include "compaction_engine.h"
#include "context_dnn.h"
#include "httplib_transport.h"
#include "skill_manager.h"
#include "../mcp/mcp_manager.h"

namespace agentlib
{

std::string agent_status_to_string(agent_status status, const std::string &tool_name)
{
	switch (status) {
		case agent_status::idle:
			return "Idle";
		case agent_status::thinking:
			return "Thinking...";
		case agent_status::tool_execution:
			if (!tool_name.empty())
				return "Running Tool: " + tool_name;
			return "Running Tool...";
		case agent_status::waiting:
			return "Waiting...";
		case agent_status::error:
			return "Error";
		default:
			return "Unknown";
	}
}

std::shared_ptr<ai_agent> ai_agent::create(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue,
					   document_provider *doc_provider)
{
	auto agent = std::shared_ptr<ai_agent>(new ai_agent(id, name, std::move(model), queue, doc_provider));
	// Don't auto-load active state automatically here because it might overwrite system prompts injected right after creation.
	// We'll let the window/caller decide when to load. Actually, if we load it here, it will be an empty shell if not found.
	return agent;
}

void ai_agent::coalesce_tool_calls(std::vector<tool_call> &tool_calls, std::unordered_map<std::string, std::string> &merged_to_parent,
				   std::unordered_map<std::string, std::pair<int, int>> &parent_ranges)
{
	struct read_call_info {
		size_t index;
		std::string path;
		int start_line;
		int end_line;
		std::string id;
	};

	std::unordered_map<std::string, std::vector<read_call_info>> file_reads;
	for (size_t i = 0; i < tool_calls.size(); ++i) {
		const auto &call = tool_calls[i];
		if (call.function.name == "fs_read_lines") {
			try {
				auto args_json = nlohmann::json::parse(call.function.arguments);
				if (args_json.is_object() && args_json.contains("path") && args_json["path"].is_string()) {
					std::string path = args_json["path"].get<std::string>();
					int start = 1;
					int end = 1000000;
					if (args_json.contains("start_line") && args_json["start_line"].is_number_integer()) {
						start = args_json["start_line"].get<int>();
					}
					if (args_json.contains("end_line") && args_json["end_line"].is_number_integer()) {
						end = args_json["end_line"].get<int>();
					}
					if (start < 1)
						start = 1;
					if (end < start)
						end = start;

					file_reads[path].push_back({i, path, start, end, call.id});
				}
			} catch (...) {
				// If parsing fails, just ignore and let it execute normally
			}
		}
	}

	const int gap_threshold = 20;

	for (auto &pair : file_reads) {
		auto &reads = pair.second;
		if (reads.size() < 2) {
			if (!reads.empty()) {
				parent_ranges[reads[0].id] = {reads[0].start_line, reads[0].end_line};
			}
			continue;
		}

		// Sort by start_line
		std::sort(reads.begin(), reads.end(),
			  [](const read_call_info &a, const read_call_info &b) { return a.start_line < b.start_line; });

		// Merge overlapping/adjacent ranges
		std::vector<read_call_info> merged;
		for (const auto &r : reads) {
			if (merged.empty()) {
				merged.push_back(r);
			} else {
				auto &last = merged.back();
				if (last.end_line + 1 + gap_threshold >= r.start_line) {
					// Merge
					last.end_line = std::max(last.end_line, r.end_line);
					// Map this call to the parent
					merged_to_parent[r.id] = last.id;
				} else {
					merged.push_back(r);
				}
			}
		}

		// Update the original tool_calls in tool_calls
		// and populate parent_ranges
		for (const auto &m : merged) {
			auto &parent_call = tool_calls[m.index];
			nlohmann::json new_args;
			new_args["path"] = pair.first;
			new_args["start_line"] = m.start_line;
			new_args["end_line"] = m.end_line;
			parent_call.function.arguments = new_args.dump();

			parent_ranges[m.id] = {m.start_line, m.end_line};
		}
	}
}

bool ai_agent::page_in_context(const std::string &episode_id, int compression_level)
{
	return set_episode_state(episode_id, compression_level);
}

int ai_agent::calculate_current_tokens() const
{
	int current_active_tokens = 0;
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	std::set<std::string> accounted_episodes;

	for (const auto &msg : conversation_) {
		if (!msg.episode_id.empty() && msg.episode_level != -1 && msg.episode_level != 99) {
			if (accounted_episodes.find(msg.episode_id) == accounted_episodes.end()) {
				accounted_episodes.insert(msg.episode_id);
				auto it = episode_index_.find(msg.episode_id);
				if (it != episode_index_.end()) {
					if (msg.episode_level == 0) {
						current_active_tokens += it->second.tokens_level_0;
					} else if (msg.episode_level == 1) {
						current_active_tokens += it->second.tokens_level_1;
					} else if (msg.episode_level == 2) {
						current_active_tokens += it->second.tokens_level_2;
					}
				}
			}
		} else {
			current_active_tokens += static_cast<int>(compaction_engine::estimate_message_tokens(msg));
		}
	}
	return current_active_tokens;
}

std::vector<std::string> ai_agent::page_in_history_auto(int default_level)
{
	std::set<std::string> active_episodes;
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		for (const auto &msg : conversation_) {
			if (!msg.episode_id.empty() && msg.episode_level != -1 && msg.episode_level != 99) {
				active_episodes.insert(msg.episode_id);
			}
		}
	}

	std::vector<const episode_index_entry *> paged_out;
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		for (const auto &pair : episode_index_) {
			if (active_episodes.find(pair.first) == active_episodes.end()) {
				paged_out.push_back(&pair.second);
			}
		}
	}

	// Sort descending by episode_seq (newest/most recent first)
	std::sort(paged_out.begin(), paged_out.end(),
		  [](const episode_index_entry *a, const episode_index_entry *b) { return a->episode_seq > b->episode_seq; });

	int current_tokens = calculate_current_tokens();
	int max_tokens = model_ ? model_->get_max_context_tokens() : 250000;
	int limit_tokens = static_cast<int>(max_tokens * 0.5);

	std::vector<std::string> paged_in_ids;

	for (const auto *entry : paged_out) {
		int ep_tokens = 0;
		if (default_level == 0) ep_tokens = entry->tokens_level_0;
		else if (default_level == 1) ep_tokens = entry->tokens_level_1;
		else if (default_level == 2) ep_tokens = entry->tokens_level_2;
		if (ep_tokens <= 0) ep_tokens = entry->tokens_level_0;

		std::stringstream pointer_msg;
		pointer_msg << "[SYSTEM MEMORY: Episode Archived]\n";
		pointer_msg << "Title: " << entry->title << "\n";
		pointer_msg << "Summary: " << entry->summary << "\n";
		if (!entry->tags.empty()) {
			pointer_msg << "Tags: [";
			for (size_t i = 0; i < entry->tags.size(); ++i) {
				pointer_msg << entry->tags[i] << (i < entry->tags.size() - 1 ? ", " : "");
			}
			pointer_msg << "]\n";
		}
		pointer_msg << "Raw history archive: " << entry->id;

		message anchor_msg;
		anchor_msg.role = "system";
		anchor_msg.content = pointer_msg.str();
		int anchor_tokens = static_cast<int>(compaction_engine::estimate_message_tokens(anchor_msg));

		bool has_anchor = false;
		{
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			auto anchor_it = std::find_if(conversation_.begin(), conversation_.end(), [&](const message &m) {
				return m.role == "system" && m.content.find("Raw history archive: " + entry->id) != std::string::npos;
			});
			if (anchor_it != conversation_.end()) {
				has_anchor = true;
			}
		}

		int net_change = ep_tokens - (has_anchor ? anchor_tokens : 0);
		if (current_tokens + net_change <= limit_tokens) {
			if (set_episode_state(entry->id, default_level)) {
				current_tokens += net_change;
				paged_in_ids.push_back(entry->id);
			}
		} else {
			// Stop once we hit the 50% limit
			break;
		}
	}

	active_tokens_.store(current_tokens);
	return paged_in_ids;
}

bool ai_agent::set_episode_state(const std::string &episode_id, int target_level)
{
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	if (!std::filesystem::exists(filepath))
		return false;

	std::string title = "Archived Episode";
	std::string summary = "Summary not available.";
	std::vector<std::string> tags;
	std::vector<message> loaded_msgs;

	try {
		std::ifstream file(filepath);
		nlohmann::json root;
		file >> root;

		if (root.contains("title"))
			title = root["title"].get<std::string>();
		if (root.contains("summary"))
			summary = root["summary"].get<std::string>();
		if (root.contains("tags"))
			tags = root["tags"].get<std::vector<std::string>>();

		if (root.contains("conversation") && root["conversation"].is_array() && target_level != 99) {
			for (const auto &item : root["conversation"]) {
				message msg;
				from_json(item, msg);

				// Assign transient properties
				msg.episode_id = episode_id;
				msg.episode_level = target_level;

				// Level 1: Strip explicit reasoning content natively provided by models like DeepSeek.
				if (target_level >= 1 && msg.role == "assistant") {
					if (msg.reasoning_content) {
						msg.reasoning_content.reset();
						increment_stat("explicit_think_blocks_stripped");
					}
				}

				// Level 2: Strip conversational pseudo-reasoning (used by GPT-4o/Gemini) if a tool call was made.
				if (target_level >= 2 && msg.role == "assistant") {
					if (msg.tool_calls && !msg.tool_calls->empty() && !msg.content.empty()) {
						msg.content.clear();
						increment_stat("pseudo_think_blocks_stripped");
					}
				}

				loaded_msgs.push_back(msg);
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Error loading episode {}: {}", episode_id, e.what());
		return false;
	}

	std::lock_guard<std::mutex> lock(conversation_mutex_);

	// Look for existing active turns matching episode_id in memory
	auto first_it = std::find_if(conversation_.begin(), conversation_.end(), [&](const message &m) {
		if (m.episode_id == episode_id)
			return true;
		if (m.role == "system" && m.content.find("[SYSTEM MEMORY: Episode Archived]") != std::string::npos) {
			size_t arch_pos = m.content.find("Raw history archive: ");
			if (arch_pos != std::string::npos) {
				std::string parsed_id = m.content.substr(arch_pos + 21);
				while (!parsed_id.empty() && (parsed_id.back() == '\r' || parsed_id.back() == '\n' ||
							      parsed_id.back() == ' ' || parsed_id.back() == '\t')) {
					parsed_id.pop_back();
				}
				if (parsed_id == episode_id)
					return true;
			}
		}
		return false;
	});

	if (first_it != conversation_.end()) {
		// Locate the end of the range
		auto last_it = first_it;
		while (last_it != conversation_.end() && last_it->episode_id == episode_id) {
			++last_it;
		}

		if (target_level == 99) {
			// Page OUT: Replace the range with a single Anchor pointer message
			std::stringstream pointer_msg;
			pointer_msg << "[SYSTEM MEMORY: Episode Archived]\n";
			pointer_msg << "Title: " << title << "\n";
			pointer_msg << "Summary: " << summary << "\n";
			if (!tags.empty()) {
				pointer_msg << "Tags: [";
				for (size_t i = 0; i < tags.size(); ++i) {
					pointer_msg << tags[i] << (i < tags.size() - 1 ? ", " : "");
				}
				pointer_msg << "]\n";
			}
			pointer_msg << "Raw history archive: " << episode_id;

			message anchor_msg;
			anchor_msg.role = "system";
			anchor_msg.content = pointer_msg.str();
			anchor_msg.episode_id = episode_id;
			anchor_msg.episode_level = 99;

			auto next_it = conversation_.erase(first_it, last_it);
			conversation_.insert(next_it, anchor_msg);
			event_logger::get_instance().log("Agent {} paged OUT context {}", name_, episode_id);
			increment_stat("context_pages_out");
		} else {
			// Transition active level: Replace range with loaded_msgs
			auto next_it = conversation_.erase(first_it, last_it);
			conversation_.insert(next_it, loaded_msgs.begin(), loaded_msgs.end());
			event_logger::get_instance().log("Agent {} shifted context level {} to {}", name_, episode_id, target_level);
			increment_stat("context_pages_compacted");
		}
	} else {
		// Not active in memory. Locate the Anchor message.
		auto anchor_it = std::find_if(conversation_.begin(), conversation_.end(), [&](const message &m) {
			return m.role == "system" && m.content.find("Raw history archive: " + episode_id) != std::string::npos;
		});

		if (anchor_it != conversation_.end()) {
			if (target_level == 99) {
				// Already paged out, nothing to do
				return true;
			}
			// Page IN: Replace the anchor with loaded_msgs
			auto next_it = conversation_.erase(anchor_it);
			conversation_.insert(next_it, loaded_msgs.begin(), loaded_msgs.end());
			event_logger::get_instance().log("Agent {} paged IN context {} at level {}", name_, episode_id, target_level);
			increment_stat("context_pages_in");
		} else {
			if (target_level == 99) {
				// Already not in memory and no anchor exists, nothing to do
				return true;
			}
			// Fallback: Append the loaded messages to the end of the conversation
			conversation_.insert(conversation_.end(), loaded_msgs.begin(), loaded_msgs.end());
			event_logger::get_instance().log("Agent {} paged IN context {} at level {} (appended)", name_, episode_id,
							 target_level);
			increment_stat("context_pages_in");
		}
	}

	// Update LRU access sequence
	long long l_seq = next_lru_seq_++;
	if (episode_index_.find(episode_id) != episode_index_.end()) {
		episode_index_[episode_id].lru_seq = l_seq;
	}

	// Update metadata file
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";
	try {
		std::ifstream mfile(meta_filepath);
		nlohmann::json meta_root;
		mfile >> meta_root;
		meta_root["lru_seq"] = l_seq;
		std::ofstream mfile_out(meta_filepath);
		mfile_out << meta_root.dump(4);
	} catch (...) {
	}

	// Trigger UI update
	if (global_queue_) {
		editor_event ev;
		ev.type = event_type::agent_tool_update;
		ev.key_code = id_;
		global_queue_->push(ev);
	}

	return true;
}
ai_agent::ai_agent(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue, document_provider *doc_provider)
    : id_(id), name_(name), model_(std::move(model)), global_queue_(queue), doc_provider_(doc_provider)
{
	auto http_transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
	client_ = std::make_unique<llm_client>(http_transport, model_->get_id(), model_->get_api_type());

	summary_thread_ = std::thread(&ai_agent::summary_worker_loop, this);
}

ai_agent::~ai_agent()
{
	close();

	{
		std::lock_guard<std::mutex> lock(summary_mutex_);
		// is_closed_ is set to true by close(), which we just called
		// Wake up worker to exit
	}
	summary_cv_.notify_all();

	if (summary_thread_.joinable()) {
		summary_thread_.join();
	}
}

void ai_agent::save_active_state() const
{
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/active_state.json";
	save_conversation(filepath);
}

bool ai_agent::load_active_state(bool fresh_agent)
{
	if (fresh_agent) {
		event_logger::get_instance().log("--fresh-agent is set, skipping history load.");
		return false;
	}

	load_episode_index();

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/active_state.json";
	if (!std::filesystem::exists(filepath))
		return false;

	try {
		std::ifstream file(filepath);
		nlohmann::json root;
		file >> root;
		if (root.contains("final_result") && root["final_result"].is_string()) {
			std::lock_guard<std::mutex> state_lock(const_cast<std::mutex &>(state_mutex_));
			final_result_ = root["final_result"].get<std::string>();
		}

		if (root.contains("conversation") && root["conversation"].is_array()) {
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			conversation_.clear();
			for (const auto &item : root["conversation"]) {
				message msg;
				from_json(item, msg);
				if (msg.episode_id.empty() && msg.role == "system" &&
				    msg.content.find("[SYSTEM MEMORY: Episode Archived]") != std::string::npos) {
					size_t arch_pos = msg.content.find("Raw history archive: ");
					if (arch_pos != std::string::npos) {
						std::string parsed_id = msg.content.substr(arch_pos + 21);
						while (!parsed_id.empty() && (parsed_id.back() == '\r' || parsed_id.back() == '\n' ||
									      parsed_id.back() == ' ' || parsed_id.back() == '\t')) {
							parsed_id.pop_back();
						}
						msg.episode_id = parsed_id;
						msg.episode_level = 99;
					}
				}
				conversation_.push_back(msg);
			}

			// Normalizer: Ensure that every tool response message immediately follows
			// the assistant message containing its tool_call definition.
			// This satisfies the strict sequencing required by LLM APIs (OpenAI/Gemini/Jinja templates).
			std::vector<message> normalized_convo;
			std::map<std::string, message> tool_responses;

			// 1. Extract all tool responses
			for (const auto &msg : conversation_) {
				if (msg.role == "tool" && msg.tool_call_id) {
					tool_responses[*msg.tool_call_id] = msg;
				}
			}

			// 2. Reconstruct the conversation in the correct order
			for (const auto &msg : conversation_) {
				if (msg.role == "tool") {
					// Skip tool messages; they will be inserted right after their corresponding assistant messages
					continue;
				}

				normalized_convo.push_back(msg);

				if (msg.role == "assistant" && msg.tool_calls) {
					for (const auto &tc : *msg.tool_calls) {
						auto it = tool_responses.find(tc.id);
						if (it != tool_responses.end()) {
							normalized_convo.push_back(it->second);
							tool_responses.erase(it);
						} else {
							// Pending tool call with no response: Create an abort message
							message abort_msg;
							abort_msg.role = "tool";
							abort_msg.tool_call_id = tc.id;
							abort_msg.name = tc.function.name;
							abort_msg.content =
							    "Tool execution aborted: Editor session was restarted before completion.";
							normalized_convo.push_back(abort_msg);

							event_logger::get_instance().log("Aborted pending tool call: {} ({})", tc.id,
											 tc.function.name);
						}
					}
				}
			}

			// 3. Discard any orphan tool responses to prevent API sequencing violations.
			// These are tool messages that have no matching assistant tool call in the loaded context
			// (e.g. because the assistant message was paged out / compressed).
			if (!tool_responses.empty()) {
				event_logger::get_instance().log(
				    "Discarded {} orphaned tool response(s) with no matching assistant tool call in active context.",
				    tool_responses.size());
			}

			conversation_ = std::move(normalized_convo);

			event_logger::get_instance().log("Agent {} restored active state from {}", name_, filepath);
			return true;
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to restore active state: {}", std::string(e.what()));
	}
	return false;
}

void ai_agent::close()
{
	if (!is_closed_) {
		// Implicit Episode: Page out all uncompressed history when the editor closes
		// so the agent boots up "fresh" (but with pointers) next session.
		page_out_prior_context("", true, "End of Session",
				       "The user closed the editor or agent window. This session was automatically paged out.",
				       {"session-end"});

		save_active_state();
	}
	is_closed_ = true;
	cancel_current_task();
}
void ai_agent::set_status(agent_status s, int target_id)
{
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		status_ = s;
		if (s == agent_status::waiting) {
			waiting_on_id_ = target_id;
		} else {
			waiting_on_id_ = -1;
		}
	}
	status_cv_.notify_all();

	if (global_queue_) {
		editor_event tool_ev;
		tool_ev.type = event_type::agent_tool_update;
		tool_ev.key_code = id_;
		global_queue_->push(tool_ev);
	}
}

void ai_agent::wait_until_idle()
{
	std::unique_lock<std::mutex> lock(state_mutex_);
	status_cv_.wait(lock, [this]() { return status_ == agent_status::idle || status_ == agent_status::error; });
}

void ai_agent::cancel_current_task()
{
	if (client_) {
		client_->cancel();
	}
	{
		std::lock_guard<std::mutex> lock(background_transport_mutex_);
		if (background_transport_) {
			background_transport_->cancel();
		}
	}
}

void ai_agent::add_todo(const std::string &task)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	todos_.push_back({task, false});
}

std::vector<todo_item> ai_agent::get_todos() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return todos_;
}

std::optional<std::string> ai_agent::pop_todo()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (todos_.empty())
		return std::nullopt;

	std::string text = todos_.front().text;
	todos_.erase(todos_.begin());

	if (global_queue_) {
		editor_event tool_ev;
		tool_ev.type = event_type::agent_tool_update;
		tool_ev.key_code = id_;
		global_queue_->push(tool_ev);
	}

	return text;
}

void ai_agent::add_active_skill(const std::string &skill_name)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	// Check if it already exists to avoid duplicates
	if (std::find(active_skills_.begin(), active_skills_.end(), skill_name) == active_skills_.end()) {
		active_skills_.push_back(skill_name);
		if (global_queue_) {
			editor_event tool_ev;
			tool_ev.type = event_type::agent_tool_update;
			tool_ev.key_code = id_;
			global_queue_->push(tool_ev);
		}
	}
}

std::vector<std::string> ai_agent::get_active_skills() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return active_skills_;
}

void ai_agent::add_active_tool_family(const std::string &family_name)
{
	{
		std::lock_guard<std::mutex> lock(state_mutex_);
		if (std::find(active_tool_families_.begin(), active_tool_families_.end(), family_name) == active_tool_families_.end()) {
			active_tool_families_.push_back(family_name);
			if (global_queue_) {
				editor_event tool_ev;
				tool_ev.type = event_type::agent_tool_update;
				tool_ev.key_code = id_;
				global_queue_->push(tool_ev);
			}
		}
	}
	update_system_prompt_with_families();
}

std::vector<std::string> ai_agent::get_active_tool_families() const
{
	std::vector<std::string> families = {"base"};

	// Add dynamically activated ones
	{
		std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
		for (const auto &fam : active_tool_families_) {
			if (std::find(families.begin(), families.end(), fam) == families.end()) {
				families.push_back(fam);
			}
		}
	}

	// Add configured families (from global/project config or active MCP servers)
	auto registered_families = tool_registry::get_instance().get_all_registered_families();
	for (const auto &fam : registered_families) {
		if (is_tool_family_active(fam)) {
			if (std::find(families.begin(), families.end(), fam) == families.end()) {
				families.push_back(fam);
			}
		}
	}

	return families;
}

bool ai_agent::is_tool_family_active(const std::string &family_name) const
{
	if (family_name == "base") {
		return true;
	}

	// Check if dynamically activated for this agent session
	{
		std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
		if (std::find(active_tool_families_.begin(), active_tool_families_.end(), family_name) != active_tool_families_.end()) {
			return true;
		}
	}

	// Check if enabled in configuration (global or project)
	config_manager &cfg = config_manager::get_instance();
	if (cfg.is_tool_family_enabled(family_name, true) || cfg.is_tool_family_enabled(family_name, false)) {
		return true;
	}

	// Check if it's an enabled MCP server family
	auto server = mcp_manager::get_instance().find_server(family_name);
	if (server && server->is_enabled()) {
		return true;
	}

	return false;
}

void ai_agent::update_system_prompt_with_families()
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (conversation_.empty()) {
		return;
	}

	// Find the first system message
	for (auto &msg : conversation_) {
		if (msg.role == "system") {
			// If we haven't stashed the original system prompt, stash it now
			if (original_system_prompt_.empty()) {
				original_system_prompt_ = msg.content;
			}

			// Rebuild the system prompt content
			std::string families_str;
			auto families = get_active_tool_families();
			for (size_t i = 0; i < families.size(); ++i) {
				if (i > 0) {
					families_str += ", ";
				}
				families_str += std::format("'{}'", families[i]);
			}

			std::string table_str;
			auto registered_families = tool_registry::get_instance().get_all_registered_families();
			std::vector<std::string> inactive_families;
			for (const auto &fam : registered_families) {
				if (fam != "base" && !is_tool_family_active(fam)) {
					inactive_families.push_back(fam);
				}
			}
			std::sort(inactive_families.begin(), inactive_families.end());

			if (!inactive_families.empty()) {
				table_str = "\n\nIf you need to use tools from another family, you must call the `activate_tool_family` "
					    "tool. Here are the available tool families and when to activate them:\n\n"
					    "| Tool Family | When to Activate |\n"
					    "| --- | --- |\n";
				for (const auto &fam : inactive_families) {
					std::string reason;
					if (fam == "x86") {
						reason = "Activate when working with x86 assembly";
					} else {
						std::string cached = config_manager::get_instance().get_mcp_server_when_to_activate(fam, false);
						if (cached.empty()) {
							cached = config_manager::get_instance().get_mcp_server_when_to_activate(fam, true);
						}
						if (!cached.empty()) {
							reason = cached;
						} else {
							reason = std::format("Activate when needing tools from the {} family", fam);
						}
					}
					table_str += std::format("| {} | {} |\n", fam, reason);
				}
			}

			msg.content = std::format("{}\n\n*** ACTIVE TOOL FAMILIES ***\n"
						  "The following tool families are currently active and available: [{}].{}",
						  original_system_prompt_, families_str, table_str);
			break;
		}
	}
}

void ai_agent::increment_stat(const std::string &key, int amount)
{
	std::lock_guard<std::mutex> lock(stats_mutex_);
	stats_[key] += amount;
}

std::map<std::string, int> ai_agent::get_stats() const
{
	std::lock_guard<std::mutex> lock(stats_mutex_);
	std::map<std::string, int> out = stats_;
	out["tokens_tx"] = tokens_tx_.load();
	out["tokens_rx"] = tokens_rx_.load();
	out["tokens_cached"] = tokens_cached_.load();
	out["estimated_cost_cents"] = static_cast<int>(estimated_cost_.load() * 100);
	return out;
}
bool ai_agent::mark_todo_complete(const std::string &text_match, std::string &out_error)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (text_match == "*") {
		for (auto &todo : todos_) {
			todo.completed = true;
		}
		return true;
	}
	int match_idx = -1;
	for (size_t i = 0; i < todos_.size(); ++i) {
		if (todos_[i].text.find(text_match) != std::string::npos) {
			if (match_idx != -1) {
				out_error = "Multiple tasks match '" + text_match + "'. Please be more specific.";
				return false;
			}
			match_idx = i;
		}
	}

	if (match_idx == -1) {
		out_error = "No task found matching '" + text_match + "'.";
		return false;
	}

	todos_[match_idx].completed = true;
	return true;
}

bool ai_agent::delete_todo(const std::string &text_match, std::string &out_error)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (text_match == "*") {
		todos_.clear();
		return true;
	}
	int match_idx = -1;
	for (size_t i = 0; i < todos_.size(); ++i) {
		if (todos_[i].text.find(text_match) != std::string::npos) {
			if (match_idx != -1) {
				out_error = "Multiple tasks match '" + text_match + "'. Please be more specific.";
				return false;
			}
			match_idx = i;
		}
	}

	if (match_idx == -1) {
		out_error = "No task found matching '" + text_match + "'.";
		return false;
	}

	todos_.erase(todos_.begin() + match_idx);
	return true;
}

std::shared_ptr<ai_agent> ai_agent::spawn_subagent(const std::string &name)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	int new_id = id_ * 100 + static_cast<int>(subagents_.size()) + 1;
	auto subagent = ai_agent::create(new_id, name, model_, global_queue_, doc_provider_);
	subagent->set_parent(shared_from_this());
	subagents_.push_back(subagent);

	if (global_queue_) {
		editor_event tool_ev;
		tool_ev.type = event_type::agent_tool_update;
		tool_ev.key_code = id_;
		global_queue_->push(tool_ev);
	}

	return subagent;
}

void ai_agent::remove_subagent(int id)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	subagents_.erase(std::remove_if(subagents_.begin(), subagents_.end(),
					[id](const std::shared_ptr<ai_agent> &agent) { return agent->get_id() == id; }),
			 subagents_.end());

	if (global_queue_) {
		editor_event tool_ev;
		tool_ev.type = event_type::agent_tool_update;
		tool_ev.key_code = id_;
		global_queue_->push(tool_ev);
	}
}

std::vector<std::shared_ptr<ai_agent>> ai_agent::get_subagents() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return subagents_;
}

void ai_agent::add_interaction(std::shared_ptr<agent_interaction> interaction)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	for (auto &existing : interactions_) {
		existing->set_age(existing->get_age() + 1);
	}
	interaction->set_age(0);
	interactions_.push_back(std::move(interaction));
}

void ai_agent::inject_context(const std::string &role, const std::string &content, bool trigger_processing)
{
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		message context_msg;
		context_msg.role = role;
		context_msg.content = content;
		conversation_.push_back(context_msg);
	}

	if (role == "system") {
		update_system_prompt_with_families();
	}

	add_interaction(std::make_shared<interaction_system_message>(content));

	if (trigger_processing && status_ == agent_status::idle) {
		start_processing();
	}
}
void ai_agent::set_model(std::shared_ptr<ai_model> model)
{
	if (!model)
		return;

	{
		std::lock_guard lock(state_mutex_);
		model_ = std::move(model);
		auto http_transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
		client_ = std::make_unique<llm_client>(http_transport, model_->get_id(), model_->get_api_type());
	}

	add_interaction(
	    std::make_shared<interaction_system_message>("Model switched to: " + model_->get_name() + " (" + model_->get_id() + ")"));
}

void ai_agent::submit_prompt(const std::string &prompt_text)
{
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		message user_msg;
		user_msg.role = "user";
		user_msg.content = prompt_text;
		user_msg.timestamp =
		    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		conversation_.push_back(user_msg);
	}

	if (!prompt_text.empty()) {
		add_interaction(std::make_shared<interaction_user_message>(prompt_text));
	}

	if (status_ == agent_status::idle) {
		start_processing();
	}
}

void ai_agent::start_processing()
{
	// Atomically check if we are already busy
	agent_status expected = agent_status::idle;
	if (!status_.compare_exchange_strong(expected, agent_status::thinking)) {
		expected = agent_status::waiting;
		if (!status_.compare_exchange_strong(expected, agent_status::thinking)) {
			// Already processing or in error state
			return;
		}
	}

	if (global_queue_) {
		editor_event tool_ev;
		tool_ev.type = event_type::agent_tool_update;
		tool_ev.key_code = id_;
		global_queue_->push(tool_ev);
	}

	std::thread([self = shared_from_this()]() {
		event_logger::get_instance().log("Thread started: ai_agent main loop ({})", self->id_);
		size_t last_synced_index = 0;
		std::vector<message> convo;

		auto &registry = tool_registry::get_instance();
		tool_context ctx;

		std::filesystem::path workspace_root(project_manager::get_instance().get_project_root());
		if (workspace_root.empty() || !std::filesystem::exists(workspace_root)) {
			workspace_root = std::filesystem::current_path();
		}

		ctx.fs_security.set_working_directory(workspace_root);
		ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
		ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
		ctx.fs_security.set_vfs(skill_manager::get_instance().get_vfs());
		ctx.doc_provider = self->doc_provider_;
		ctx.queue = self->global_queue_;
		ctx.active_agent = self.get();
		ctx.is_family_active = [self](const std::string &family) { return self->is_tool_family_active(family); };

		std::string final_response;

		while (true) {
			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed early]", self->id_);
				return;
			}

			// Sync with shared conversation history
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				for (size_t i = last_synced_index; i < self->conversation_.size(); ++i) {
					convo.push_back(self->conversation_[i]);
				}
				last_synced_index = self->conversation_.size();
			}

			self->evaluate_compaction();
			self->evaluate_auto_episode(convo);

			// Reload/Sync with shared conversation history in case compaction modified it
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				convo.clear();
				for (const auto &msg : self->conversation_) {
					convo.push_back(msg);
				}
				last_synced_index = self->conversation_.size();
			}

			self->set_status(agent_status::thinking);

			std::shared_ptr<interaction_reasoning> current_reasoning = nullptr;
			std::shared_ptr<interaction_llm_response> current_response = nullptr;
			std::vector<tool_call> accumulated_tool_calls;
			message response_msg;
			response_msg.role = "assistant";

			auto start_time = std::chrono::steady_clock::now();
			auto start_timestamp =
			    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			self->client_->send_chat_stream(
			    convo,
			    [&](const chat_delta &delta) {
				    if (self->is_closed_)
					    return;

				    if (!delta.reasoning_content.empty()) {
					    if (!current_reasoning) {
						    current_reasoning = std::make_shared<interaction_reasoning>("");
						    self->add_interaction(current_reasoning);
					    }
					    current_reasoning->append_text(delta.reasoning_content);
					    if (!response_msg.reasoning_content) {
						    response_msg.reasoning_content = "";
					    }
					    *response_msg.reasoning_content += delta.reasoning_content;
					    if (self->global_queue_) {
						    editor_event ev;
						    ev.type = event_type::agent_tool_update;
						    ev.key_code = self->id_;
						    self->global_queue_->push(ev);
					    }
				    }

				    if (!delta.content.empty()) {
					    if (!current_response) {
						    current_response = std::make_shared<interaction_llm_response>("");
						    self->add_interaction(current_response);
					    }
					    current_response->append_text(delta.content);
					    response_msg.content += delta.content;
					    if (self->global_queue_) {
						    editor_event ev;
						    ev.type = event_type::agent_tool_update;
						    ev.key_code = self->id_;
						    self->global_queue_->push(ev);
					    }
				    }

				    if (delta.tool_calls) {
					    for (auto tc : *delta.tool_calls) {
						    if (tc.id.empty() && !accumulated_tool_calls.empty()) {
							    auto &last = accumulated_tool_calls.back();
							    if (!tc.function.name.empty())
								    last.function.name += tc.function.name;
							    if (!tc.function.arguments.empty())
								    last.function.arguments += tc.function.arguments;
							    if (tc.signature)
								    last.signature = tc.signature;
						    } else {
							    if (tc.id.empty()) {
								    tc.id = "call_" + std::to_string(std::rand());
							    }
							    accumulated_tool_calls.push_back(tc);
						    }
					    }
				    }

				    if (delta.usage.total_tokens > 0) {
					    self->tokens_tx_ += delta.usage.prompt_tokens;
					    self->tokens_rx_ += delta.usage.completion_tokens;
					    self->tokens_cached_ = delta.usage.cached_tokens; // Update to latest cached amount

					    double turn_cost = self->model_->calculate_and_record_cost(delta.usage.prompt_tokens,
												       delta.usage.completion_tokens);
					    double current_cost = self->estimated_cost_.load();
					    self->estimated_cost_.store(current_cost + turn_cost);

					    if (self->global_queue_) {
						    editor_event tool_ev;
						    tool_ev.type = event_type::agent_tool_update;
						    tool_ev.key_code = self->id_;
						    self->global_queue_->push(tool_ev);
					    }
				    }
			    },
			    &registry, self->get_active_tool_families());

			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed early]", self->id_);
				return;
			}

			auto end_time = std::chrono::steady_clock::now();
			response_msg.timestamp = start_timestamp;
			response_msg.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

			if (!accumulated_tool_calls.empty()) {
				for (auto &call : accumulated_tool_calls) {
					normalize_tool_call(call);
				}
				response_msg.tool_calls = accumulated_tool_calls;
			}

			// Commit assistant response to shared history
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				self->conversation_.push_back(response_msg);
				last_synced_index = self->conversation_.size();
			}
			convo.push_back(response_msg);

			if (!accumulated_tool_calls.empty()) {
				std::unordered_map<std::string, std::string> merged_to_parent;
				std::unordered_map<std::string, std::pair<int, int>> parent_ranges;
				coalesce_tool_calls(accumulated_tool_calls, merged_to_parent, parent_ranges);

				for (const auto &call : accumulated_tool_calls) {
					if (self->is_closed_) {
						event_logger::get_instance().log(
						    "Thread exited: ai_agent main loop ({}) [closed in callback]", self->id_);
						return;
					}

					self->current_tool_ = call.function.name;
					self->set_status(agent_status::tool_execution);

					std::string arg_preview;
					try {
						auto args_json = nlohmann::json::parse(call.function.arguments);
						if (args_json.is_object()) {
							bool first = true;
							for (const auto &item : args_json.items()) {
								if (!first)
									arg_preview += ", ";
								first = false;

								std::string val_str;
								if (item.value().is_string()) {
									std::string s = item.value().get<std::string>();
									std::replace(s.begin(), s.end(), '\n', ' ');
									std::replace(s.begin(), s.end(), '\r', ' ');
									if (s.length() > 40)
										s = s.substr(0, 37) + "...";
									val_str = "\"" + s + "\"";
								} else {
									std::string s = item.value().dump();
									if (s.length() > 40)
										s = s.substr(0, 37) + "...";
									val_str = s;
								}
								arg_preview += val_str;
							}
						}
					} catch (...) {
						arg_preview = "...";
					}

					bool is_silent = registry.is_tool_silent(call.function.name);
					if (config_manager::get_instance().is_log_all_tool_calls()) {
						is_silent = false;
					}

					// Provide UI update callback for long-running tools
					ctx.trigger_ui_update = [this_ptr = self.get()]() {
						if (this_ptr->global_queue_) {
							editor_event ev;
							ev.type = event_type::agent_tool_update;
							ev.key_code = this_ptr->id_;
							this_ptr->global_queue_->push(ev);
						}
					};

					bool is_merged = (merged_to_parent.find(call.id) != merged_to_parent.end());
					std::string tool_result;
					std::shared_ptr<agent_interaction> custom_interaction;
					long long tool_start_timestamp = 0;
					long long tool_duration_ms = 0;

					if (is_merged) {
						std::string parent_id = merged_to_parent[call.id];
						int p_start = 1;
						int p_end = 1000000;
						if (parent_ranges.find(parent_id) != parent_ranges.end()) {
							p_start = parent_ranges[parent_id].first;
							p_end = parent_ranges[parent_id].second;
						}
						tool_result = std::format("Note: This read request was adjacent to/overlapping with "
									  "another read request in the same turn. "
									  "To avoid redundant output and keep the code contiguous, it has "
									  "been merged into tool call {} "
									  "(which reads lines {} - {}). Please refer to the output of that "
									  "tool call for the content.",
									  parent_id, p_start, p_end);

						if (!is_silent) {
							self->add_interaction(std::make_shared<interaction_tool_call>(
							    call.function.name,
							    std::format("{}(merged into {})", call.function.name, parent_id)));
							if (self->global_queue_) {
								editor_event tool_ev;
								tool_ev.type = event_type::agent_tool_update;
								tool_ev.key_code = self->id_;
								self->global_queue_->push(tool_ev);
							}
						}

						tool_start_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
									   std::chrono::system_clock::now().time_since_epoch())
									   .count();
					} else {
						ctx.tool_call_id = call.id;
						auto prep = registry.prepare_tool(call.function.name, call.function.arguments, ctx);

						if (prep.tool) {
							custom_interaction = prep.tool->get_interaction();
						}

						if (!is_silent) {
							if (custom_interaction) {
								self->add_interaction(custom_interaction);
							} else {
								self->add_interaction(std::make_shared<interaction_tool_call>(
								    call.function.name, call.function.name + "(" + arg_preview + ")"));
							}
							if (self->global_queue_) {
								editor_event tool_ev;
								tool_ev.type = event_type::agent_tool_update;
								tool_ev.key_code = self->id_;
								self->global_queue_->push(tool_ev);
							}
						}

						tool_start_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
									   std::chrono::system_clock::now().time_since_epoch())
									   .count();
						auto tool_start_time = std::chrono::steady_clock::now();

						if (!prep.error_message.empty()) {
							tool_result = prep.error_message;
						} else {
							try {
								tool_result = prep.tool->execute(ctx);
							} catch (const std::exception &e) {
								tool_result = "Execution Error: " + std::string(e.what());
							}
						}

						auto tool_end_time = std::chrono::steady_clock::now();
						tool_duration_ms =
						    std::chrono::duration_cast<std::chrono::milliseconds>(tool_end_time - tool_start_time)
							.count();
					}

					std::string result_preview = tool_result;
					if (result_preview.length() > 1024) {
						result_preview = result_preview.substr(0, 1021) + "...";
					}

					if (!is_silent && !custom_interaction) {
						self->add_interaction(
						    std::make_shared<interaction_tool_result>(call.function.name, result_preview));
						if (self->global_queue_) {
							editor_event result_ev;
							result_ev.type = event_type::agent_tool_update;
							result_ev.key_code = self->id_;
							self->global_queue_->push(result_ev);
						}
					} else if (!is_silent && custom_interaction) {
						// Force a redraw so the final status of the custom interaction (e.g. checkmark) is visible
						if (self->global_queue_) {
							editor_event result_ev;
							result_ev.type = event_type::agent_tool_update;
							result_ev.key_code = self->id_;
							self->global_queue_->push(result_ev);
						}
					}

					message tool_msg;
					tool_msg.role = "tool";
					tool_msg.content = tool_result;
					tool_msg.name = call.function.name;
					tool_msg.tool_call_id = call.id;
					tool_msg.timestamp = tool_start_timestamp;
					tool_msg.duration_ms = tool_duration_ms;

					// Commit tool result to shared history
					{
						std::lock_guard<std::mutex> lock(self->conversation_mutex_);
						self->conversation_.push_back(tool_msg);
						last_synced_index = self->conversation_.size();
					}
					convo.push_back(tool_msg);

					// Attempt to zap transient failure loops now that a tool has completed
					self->compact_ephemeral_errors(convo);
				}
				self->current_tool_.clear();
				self->set_status(agent_status::thinking);
			} else {
				final_response = response_msg.content;

				bool more_user_input = false;
				{
					std::lock_guard<std::mutex> lock(self->conversation_mutex_);
					if (last_synced_index < self->conversation_.size()) {
						more_user_input = true;
					}
				}
				if (more_user_input) {
					continue; // Loop around to instantly process the queued user prompt!
				}

				self->evaluate_compaction();
				self->evaluate_auto_episode(convo);
				last_synced_index = self->conversation_.size();
				break;
			}
		}

		if (self->is_closed_) {
			event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed at end]", self->id_);
			return;
		}

		self->set_status(agent_status::idle);
		event_logger::get_instance().log("Agent {} went idle. Cumulative tokens: Tx={} Rx={} Cached={}", self->id_,
						 self->tokens_tx_.load(), self->tokens_rx_.load(), self->tokens_cached_.load());

		if (self->global_queue_) {
			editor_event ev;
			ev.type = event_type::agent_response;
			ev.key_code = self->id_;
			ev.payload = final_response;
			self->global_queue_->push(ev);
		}

		// Notify parent agent asynchronously
		if (auto parent = self->parent_agent_.lock()) {
			std::string agent_id_str = std::to_string(self->id_);
			std::string uri = "agent://" + agent_id_str + "/completion_report.json";

			std::string summary_text;
			if (!final_response.empty()) {
				// Heuristic: Extract the last 120 characters and remove newlines
				if (final_response.length() > 120) {
					summary_text = final_response.substr(final_response.length() - 120);
				} else {
					summary_text = final_response;
				}

				// Replace all newlines with spaces
				std::replace(summary_text.begin(), summary_text.end(), '\n', ' ');
				std::replace(summary_text.begin(), summary_text.end(), '\r', ' ');

				// Clean up leading/trailing whitespace
				summary_text.erase(0, summary_text.find_first_not_of(" \t"));
				summary_text.erase(summary_text.find_last_not_of(" \t") + 1);
			} else {
				summary_text = "Task completed with no final response text.";
			}

			nlohmann::json notification_json = {{"event", "SubagentStop"},
							    {"agent_id", std::to_string(self->id_)},
							    {"name", self->name_},
							    {"status", "completed"},
							    {"result", {{"summary", summary_text}, {"output_path", "`" + uri + "`"}}}};

			// Generate full history buffer for the virtual file
			std::string full_history = "Interaction History for Agent " + agent_id_str + " (" + self->name_ +
						   ")\n=======================================================\n\n";
			for (const auto &interaction : self->interactions_) {
				full_history += interaction->get_raw_text() + "\n\n";
			}

			// Mount to global VFS
			skill_manager::get_instance().get_vfs()->mount_buffer(uri, full_history);

			std::string system_msg =
			    "Subagent " + agent_id_str + " (" + self->name_ + ") has finished processing and returned to idle state.\n\n";
			system_msg += "Completion Event Data:\n```json\n" + notification_json.dump(2) + "\n```\n\n";
			system_msg += "You can read the full interaction history log with the fs_read_lines tool from `" + uri + "`";

			parent->inject_context("user", system_msg, true);
		}
	}).detach();
}

void ai_agent::save_conversation(const std::string &filepath) const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	nlohmann::json root;
	root["agent_id"] = id_;
	root["agent_name"] = name_;
	{
		std::lock_guard<std::mutex> state_lock(const_cast<std::mutex &>(state_mutex_));
		root["final_result"] = final_result_;
	}
	nlohmann::json conv_array = nlohmann::json::array();
	for (const auto &msg : conversation_) {
		nlohmann::json m_json;
		to_json(m_json, msg);
		conv_array.push_back(m_json);
	}
	root["conversation"] = conv_array;

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
	}
}

void ai_agent::snapshot_episode(const std::string &title, const std::string &summary, const std::vector<std::string> &tags)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);

	if (conversation_.empty())
		return;

	nlohmann::json block_array = nlohmann::json::array();
	int l0_chars = 0;
	int l1_chars = 0;
	int l2_chars = 0;

	for (const auto &msg : conversation_) {
		nlohmann::json m_json;
		to_json(m_json, msg);
		block_array.push_back(m_json);

		int msg_chars = m_json.dump().length();
		l0_chars += msg_chars;

		int r_chars = 0;
		if (msg.reasoning_content) {
			r_chars = msg.reasoning_content->length();
		}
		l1_chars += (msg_chars - r_chars);

		int pseudo_r_chars = 0;
		if (msg.role == "assistant" && msg.tool_calls && !msg.tool_calls->empty()) {
			pseudo_r_chars = msg.content.length();
		}
		l2_chars += (msg_chars - r_chars - pseudo_r_chars);
	}

	long long seq = next_episode_seq_++;
	std::string episode_id = "episode_" + std::to_string(seq);

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";

	nlohmann::json root;
	root["episode_id"] = episode_id;
	root["title"] = title;
	root["summary"] = summary;
	root["tags"] = tags;
	root["conversation"] = block_array;

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
		event_logger::get_instance().log("Snapshot written to {}", episode_id);
	}

	nlohmann::json meta;
	meta["episode_id"] = episode_id;
	meta["title"] = title;
	meta["summary"] = summary;
	meta["reactivation_hint"] = ""; // Filled asynchronously
	meta["tags"] = tags;
	meta["episode_seq"] = seq;
	long long l_seq = next_lru_seq_++;
	meta["lru_seq"] = l_seq;
	meta["tokens_level_0"] = l0_chars / 4;
	meta["tokens_level_1"] = l1_chars / 4;
	meta["tokens_level_2"] = l2_chars / 4;

	std::ofstream meta_file(meta_filepath);
	if (meta_file.is_open()) {
		meta_file << meta.dump(4);
	}

	episode_index_entry mi;
	mi.id = episode_id;
	mi.title = title;
	mi.summary = summary;
	mi.tags = tags;
	mi.episode_seq = seq;
	mi.lru_seq = l_seq;
	mi.tokens_level_0 = l0_chars / 4;
	mi.tokens_level_1 = l1_chars / 4;
	mi.tokens_level_2 = l2_chars / 4;
	episode_index_[episode_id] = mi;
}

void ai_agent::page_out_context(size_t start_index, size_t end_index, const std::string &title, const std::string &summary,
				const std::vector<std::string> &tags)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);

	// Identify all tool call groups in conversation_
	// A group is a pair of {g_start, g_end} (inclusive)
	// If a tool call in the assistant message is missing a response in the current conversation,
	// the group is pending and extends to the current end of the conversation (g_end = conversation_.size()).
	std::vector<std::pair<size_t, size_t>> tool_groups;
	for (size_t i = 0; i < conversation_.size(); ++i) {
		if (conversation_[i].role == "assistant" && conversation_[i].tool_calls && !conversation_[i].tool_calls->empty()) {
			size_t g_start = i;
			size_t g_end = i;
			bool has_pending = false;
			std::set<std::string> ids;
			for (const auto &tc : *conversation_[i].tool_calls) {
				ids.insert(tc.id);
			}

			for (size_t j = i + 1; j < conversation_.size(); ++j) {
				if (conversation_[j].role == "tool" && conversation_[j].tool_call_id &&
				    ids.count(*conversation_[j].tool_call_id) > 0) {
					g_end = j;
				}
			}

			for (const auto &tc : *conversation_[i].tool_calls) {
				bool found = false;
				for (size_t j = i + 1; j < conversation_.size(); ++j) {
					if (conversation_[j].role == "tool" && conversation_[j].tool_call_id &&
					    *conversation_[j].tool_call_id == tc.id) {
						found = true;
						break;
					}
				}
				if (!found) {
					has_pending = true;
					break;
				}
			}

			if (has_pending) {
				g_end = conversation_.size();
			}

			tool_groups.push_back({g_start, g_end});
		}
	}

	// Adjust boundaries iteratively until no partial intersection remains
	bool adjusted = true;
	while (adjusted) {
		adjusted = false;
		for (const auto &group : tool_groups) {
			size_t g_start = group.first;
			size_t g_end = group.second;

			if (start_index < end_index) {
				size_t active_end = end_index - 1;
				if (g_start <= active_end && g_end >= start_index) {
					if (g_start >= start_index && g_end <= active_end) {
						continue;
					}
					if (g_start < start_index) {
						start_index = g_end + 1;
						adjusted = true;
					}
					if (g_end > active_end) {
						end_index = g_start;
						adjusted = true;
					}
				}
			}
		}
	}

	if (start_index >= end_index || end_index > conversation_.size())
		return;

	// 1. Serialize the block
	nlohmann::json block_array = nlohmann::json::array();
	int l0_chars = 0;
	int l1_chars = 0;
	int l2_chars = 0;

	for (size_t i = start_index; i < end_index; ++i) {
		nlohmann::json m_json;
		to_json(m_json, conversation_[i]);
		block_array.push_back(m_json);

		int msg_chars = m_json.dump().length();
		l0_chars += msg_chars;

		int r_chars = 0;
		if (conversation_[i].reasoning_content) {
			r_chars = conversation_[i].reasoning_content->length();
		}

		l1_chars += (msg_chars - r_chars);

		int pseudo_r_chars = 0;
		if (conversation_[i].role == "assistant" && conversation_[i].tool_calls && !conversation_[i].tool_calls->empty()) {
			pseudo_r_chars = conversation_[i].content.length();
		}

		l2_chars += (msg_chars - r_chars - pseudo_r_chars);
	}

	long long seq = next_episode_seq_++;
	std::string episode_id = "episode_" + std::to_string(seq);

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";

	nlohmann::json root;
	root["episode_id"] = episode_id;
	root["title"] = title;
	root["summary"] = summary;
	root["tags"] = tags;
	root["conversation"] = block_array;

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
		file.close();
	} else {
		event_logger::get_instance().log("Failed to write episode archive to {}", filepath);
		return; // Don't delete history if we couldn't save it
	}

	nlohmann::json meta;
	meta["episode_id"] = episode_id;
	meta["title"] = title;
	meta["summary"] = summary;
	meta["reactivation_hint"] = ""; // Filled asynchronously
	meta["tags"] = tags;
	meta["episode_seq"] = seq;
	long long l_seq = next_lru_seq_++;
	meta["lru_seq"] = l_seq;
	meta["tokens_level_0"] = l0_chars / 4;
	meta["tokens_level_1"] = l1_chars / 4;
	meta["tokens_level_2"] = l2_chars / 4;

	std::ofstream meta_file(meta_filepath);
	if (meta_file.is_open()) {
		meta_file << meta.dump(4);
	}

	episode_index_entry mi;
	mi.id = episode_id;
	mi.title = title;
	mi.summary = summary;
	mi.tags = tags;
	mi.episode_seq = seq;
	mi.lru_seq = l_seq;
	mi.tokens_level_0 = l0_chars / 4;
	mi.tokens_level_1 = l1_chars / 4;
	mi.tokens_level_2 = l2_chars / 4;
	episode_index_[episode_id] = mi;

	// 2. Replace the block with the summary pointer
	std::stringstream pointer_msg;
	pointer_msg << "[SYSTEM MEMORY: Episode Archived]\n";
	pointer_msg << "Title: " << title << "\n";
	pointer_msg << "Summary: " << summary << "\n";
	if (!tags.empty()) {
		pointer_msg << "Tags: [";
		for (size_t i = 0; i < tags.size(); ++i) {
			pointer_msg << tags[i] << (i < tags.size() - 1 ? ", " : "");
		}
		pointer_msg << "]\n";
	}
	pointer_msg << "Raw history archive: " << episode_id;

	message summary_msg;
	summary_msg.role = "system";
	summary_msg.content = pointer_msg.str();
	summary_msg.episode_id = episode_id;
	summary_msg.episode_level = 99;

	conversation_.erase(conversation_.begin() + start_index, conversation_.begin() + end_index);
	conversation_.insert(conversation_.begin() + start_index, summary_msg);

	event_logger::get_instance().log("Paged out {} turns to {}", end_index - start_index, episode_id);
	increment_stat("context_pages_out");

	if (!project_manager::get_instance().is_exiting()) {
		{
			std::lock_guard<std::mutex> lock(summary_mutex_);
			summary_queue_.push_back({episode_id, filepath});
		}
		summary_cv_.notify_one();
	}
}

void ai_agent::load_episode_index()
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	episode_index_.clear();

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	if (!std::filesystem::exists(history_dir))
		return;

	for (const auto &entry : std::filesystem::directory_iterator(history_dir)) {
		std::string filename = entry.path().filename().string();
		if (entry.is_regular_file() && filename.ends_with("_metadata.json")) {
			try {
				std::ifstream f(entry.path());
				nlohmann::json root;
				f >> root;

				episode_index_entry mi;
				mi.id = root.value("episode_id", "unknown");
				mi.title = root.value("title", "Untitled");
				mi.summary = root.value("summary", "");
				mi.reactivation_hint = root.value("reactivation_hint", "");
				mi.episode_seq = root.value("episode_seq", 0LL);
				mi.lru_seq = root.value("lru_seq", mi.episode_seq);
				mi.tokens_level_0 = root.value("tokens_level_0", 0);
				mi.tokens_level_1 = root.value("tokens_level_1", 0);
				mi.tokens_level_2 = root.value("tokens_level_2", 0);

				if (root.contains("tags") && root["tags"].is_array()) {
					for (const auto &tag : root["tags"]) {
						mi.tags.push_back(tag.get<std::string>());
					}
				}

				episode_index_[mi.id] = mi;

				if (mi.episode_seq >= next_episode_seq_) {
					next_episode_seq_ = mi.episode_seq + 1;
				}
				if (mi.lru_seq >= next_lru_seq_) {
					next_lru_seq_ = mi.lru_seq + 1;
				}

				if (mi.reactivation_hint.empty() && !project_manager::get_instance().is_exiting()) {
					std::string episode_filepath = history_dir + "/" + mi.id + ".json";
					std::lock_guard<std::mutex> slock(summary_mutex_);
					summary_queue_.push_back({mi.id, episode_filepath});
					summary_cv_.notify_one();
				}
			} catch (...) {
			}
		}
	}
}

std::string ai_agent::get_memory_index() const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (episode_index_.empty()) {
		return "Memory index is empty (no saved episodes).";
	}

	std::stringstream out;
	out << "Agent Memory Index (Paged-Out Episodes):\n";

	// Sort episodes by creation date
	std::vector<const episode_index_entry *> sorted;
	for (const auto &pair : episode_index_) {
		sorted.push_back(&pair.second);
	}
	std::sort(sorted.begin(), sorted.end(),
		  [](const episode_index_entry *a, const episode_index_entry *b) { return a->episode_seq < b->episode_seq; });

	for (const auto *mi : sorted) {
		out << "- [" << mi->id << "] " << mi->title << " (~" << mi->tokens_level_0 << " raw, ~" << mi->tokens_level_1
		    << " think-free, ~" << mi->tokens_level_2 << " think-free+pseudo tokens paged-out)\n";
		if (!mi->reactivation_hint.empty()) {
			out << "  Hint: " << mi->reactivation_hint << "\n";
		}
		if (!mi->tags.empty()) {
			out << "  Tags: ";
			for (size_t i = 0; i < mi->tags.size(); ++i) {
				out << mi->tags[i] << (i < mi->tags.size() - 1 ? ", " : "");
			}
			out << "\n";
		}
	}

	return out.str();
}

void ai_agent::page_out_prior_context(const std::string &target_episode_id, bool include_all_prior, const std::string &title,
				      const std::string &summary, const std::vector<std::string> &tags)
{
	std::unique_lock<std::mutex> lock(conversation_mutex_);

	if (conversation_.size() < 3)
		return; // Nothing to compress

	size_t end_index = conversation_.size() - 2; // Default to current

	// 1. Find the upper boundary
	if (!target_episode_id.empty()) {
		bool found = false;
		// Search backwards for the specific episode marker
		for (int i = static_cast<int>(conversation_.size()) - 2; i >= 0; --i) {
			if (conversation_[i].role == "system" && conversation_[i].content.find(target_episode_id) != std::string::npos) {
				end_index = i; // The boundary is exactly at the target episode
				found = true;
				break;
			}
		}
		if (!found) {
			event_logger::get_instance().log("Failed to find target episode: {}", target_episode_id);
			return;
		}
	} else {
		// If no target provided, scan backwards to find the most recent episode marker
		// We look for either the tool result from agent_mark_episode OR a previously injected pointer.
		for (int i = static_cast<int>(conversation_.size()) - 2; i >= 0; --i) {
			if (conversation_[i].role == "tool" && conversation_[i].name == "agent_mark_episode") {
				end_index = i + 1;
				break;
			}
			if (conversation_[i].role == "system" && conversation_[i].content.find("Episode Archived") != std::string::npos) {
				end_index = i;
				break;
			}
		}
	}

	size_t start_index = 1; // Default to after the root system prompt

	// 2. Find the lower boundary
	if (!include_all_prior && end_index > 0) {
		// Scan backward from end_index to find the previous episode/system marker
		for (int i = static_cast<int>(end_index) - 1; i >= 0; --i) {
			if (conversation_[i].role == "system" || conversation_[i].role == "user") {
				start_index = i + 1;
				break;
			}
		}
	}

	if (start_index >= end_index) {
		event_logger::get_instance().log("Context too small to page out naturally.");
		return;
	}

	// Unlock and delegate to the core paging function
	lock.unlock();
	page_out_context(start_index, end_index, title, summary, tags);
}

void ai_agent::compact_ephemeral_errors(std::vector<message> &convo)
{
	bool compacted = false;

	while (convo.size() >= 4) {
		auto it_n0 = convo.end() - 1;
		auto it_n1 = convo.end() - 2;
		auto it_n2 = convo.end() - 3;
		auto it_n3 = convo.end() - 4;

		if (it_n0->role != "tool")
			break;
		if (it_n1->role != "assistant" || !it_n1->tool_calls || it_n1->tool_calls->size() != 1)
			break;
		if (it_n2->role != "tool")
			break;
		if (it_n3->role != "assistant" || !it_n3->tool_calls || it_n3->tool_calls->size() != 1)
			break;

		if (!it_n0->name || !it_n2->name)
			break;
		if (*it_n0->name != *it_n2->name)
			break;

		std::string tool_name = *it_n0->name;
		if (it_n1->tool_calls->at(0).function.name != tool_name || it_n3->tool_calls->at(0).function.name != tool_name)
			break;

		auto is_error = [](const std::string &content) {
			return content.starts_with("Error:") || content.starts_with("Verification Error:") ||
			       content.starts_with("Stage 1 Security Violation:") || content.starts_with("Stage 2 Security Violation:");
		};

		if (is_error(it_n0->content))
			break; // N-0 must be a success
		if (!is_error(it_n2->content))
			break; // N-2 must be a failure

		// We have a match! ZAP N-3 and N-2, and strip N-1
		it_n1->content.clear();
		// Reasoning isn't part of standard message serialization currently, but if we add it, we'd clear it here.
		// Wait, message struct doesn't have reasoning_content right now, it's only in chat_delta!
		// The assistant's reasoning is actually shoved into `content` in OpenAI format or it's dropped if not requested.

		convo.erase(it_n3, it_n1); // Erases N-3 and N-2
		increment_stat("ephemeral_errors_zapped");
		compacted = true;
	}

	if (compacted) {
		// Sync the exact same mutations to the global conversation array
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		while (conversation_.size() >= 4) {
			auto it_n0 = conversation_.end() - 1;
			auto it_n1 = conversation_.end() - 2;
			auto it_n2 = conversation_.end() - 3;
			auto it_n3 = conversation_.end() - 4;

			if (it_n0->role != "tool")
				break;
			if (it_n1->role != "assistant" || !it_n1->tool_calls || it_n1->tool_calls->size() != 1)
				break;
			if (it_n2->role != "tool")
				break;
			if (it_n3->role != "assistant" || !it_n3->tool_calls || it_n3->tool_calls->size() != 1)
				break;

			if (!it_n0->name || !it_n2->name)
				break;
			if (*it_n0->name != *it_n2->name)
				break;

			std::string tool_name = *it_n0->name;
			if (it_n1->tool_calls->at(0).function.name != tool_name || it_n3->tool_calls->at(0).function.name != tool_name)
				break;

			auto is_error = [](const std::string &content) {
				return content.starts_with("Error:") || content.starts_with("Verification Error:") ||
				       content.starts_with("Stage 1 Security Violation:") ||
				       content.starts_with("Stage 2 Security Violation:");
			};

			if (is_error(it_n0->content))
				break;
			if (!is_error(it_n2->content))
				break;

			it_n1->content.clear();
			conversation_.erase(it_n3, it_n1);
		}
		event_logger::get_instance().log("Agent {} zapped ephemeral errors from context.", name_);
	}
}

void ai_agent::replace_tool_result(const std::string &tool_call_id, const std::string &new_content)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	for (auto it = conversation_.rbegin(); it != conversation_.rend(); ++it) {
		if (it->role == "tool" && it->tool_call_id == tool_call_id) {
			it->content = new_content;

			// Also notify the UI that context changed silently
			if (global_queue_) {
				editor_event ev;
				ev.type = event_type::agent_tool_update;
				ev.key_code = id_;
				global_queue_->push(ev);
			}
			return;
		}
	}
}

struct parsed_turn {
	std::string prompt;
	std::string response;
	long long timestamp = 0;
	long long duration_ms = 0;
	bool is_boundary = false;
	bool git_commit = false;
	bool compile = false;
	bool test = false;
};

static std::vector<parsed_turn> parse_turns(const std::vector<message> &convo)
{
	std::vector<parsed_turn> turns;
	parsed_turn current_turn;
	bool has_current = false;

	for (const auto &msg : convo) {
		if (msg.role == "system" && msg.content.find("[SYSTEM MEMORY: Episode Archived]") != std::string::npos) {
			if (has_current) {
				turns.push_back(current_turn);
				has_current = false;
			}
			if (!turns.empty()) {
				turns.back().is_boundary = true;
			}
			continue;
		}

		if (msg.role == "user") {
			if (has_current) {
				turns.push_back(current_turn);
			}
			current_turn = parsed_turn{};
			current_turn.prompt = msg.content;
			current_turn.timestamp = msg.timestamp;
			current_turn.duration_ms = msg.duration_ms;
			has_current = true;
		} else if (msg.role == "assistant" && has_current) {
			if (!msg.content.empty()) {
				if (!current_turn.response.empty()) {
					current_turn.response += "\n" + msg.content;
				} else {
					current_turn.response = msg.content;
				}
			}
			if (msg.duration_ms > 0) {
				current_turn.duration_ms += msg.duration_ms;
			}
			if (msg.timestamp > 0) {
				current_turn.timestamp = msg.timestamp;
			}
			if (msg.tool_calls) {
				for (const auto &tc : *msg.tool_calls) {
					if (tc.function.name == "git_commit") {
						current_turn.git_commit = true;
					} else if (tc.function.name == "fs_compile_project" || tc.function.name == "fs_compile_file") {
						current_turn.compile = true;
					} else if (tc.function.name == "fs_run_tests") {
						current_turn.test = true;
					}
				}
			}
		} else if (msg.role == "tool" && has_current) {
			if (msg.name == "fs_compile_project" || msg.name == "fs_compile_file") {
				if (msg.content.find("Error:") == std::string::npos && msg.content.find("FAILED") == std::string::npos) {
					current_turn.compile = true;
				}
			} else if (msg.name == "fs_run_tests") {
				if (msg.content.find("Error:") == std::string::npos && msg.content.find("FAILED") == std::string::npos) {
					current_turn.test = true;
				}
			}
		}
	}

	if (has_current) {
		turns.push_back(current_turn);
	}
	return turns;
}

static std::string get_last_50_words(const std::string &text)
{
	std::vector<std::string> words;
	std::string current;
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!current.empty()) {
				words.push_back(current);
				current.clear();
			}
		} else {
			current += c;
		}
	}
	if (!current.empty()) {
		words.push_back(current);
	}

	if (words.size() <= 50) {
		return text;
	}

	std::string result;
	for (size_t i = words.size() - 50; i < words.size(); ++i) {
		if (!result.empty()) {
			result += " ";
		}
		result += words[i];
	}
	return result;
}

void ai_agent::evaluate_auto_episode(std::vector<message> &convo)
{
	int recent_chars = 0;
	for (int i = static_cast<int>(convo.size()) - 1; i >= 0; --i) {
		if (convo[i].role == "tool" && convo[i].name == "agent_mark_episode")
			break;
		if (convo[i].role == "system" && convo[i].content.find("Episode Archived") != std::string::npos)
			break;

		recent_chars += convo[i].content.length();
		if (convo[i].reasoning_content)
			recent_chars += convo[i].reasoning_content->length();
		if (convo[i].role == "assistant" && convo[i].tool_calls) {
			for (const auto &tc : *convo[i].tool_calls) {
				recent_chars += tc.function.arguments.length();
			}
		}
	}

	if (!convo.empty()) {
		const auto &last_msg = convo.back();
		if (last_msg.role == "tool" || (last_msg.role == "assistant" && last_msg.tool_calls && !last_msg.tool_calls->empty())) {
			return;
		}
	}

	int max_tokens = model_ ? model_->get_max_context_tokens() : 250000;
	int safeguard_tokens = static_cast<int>(max_tokens * 0.256);
	int limit_chars = safeguard_tokens * 4;

	bool should_split = false;
	float boundary_prob = -1.0f;

	if (recent_chars > limit_chars) {
		should_split = true;
		event_logger::get_instance().log("Context size exceeds {} tokens ({} chars). Forcing auto-episode boundary.",
						 safeguard_tokens, recent_chars);
	} else {
		std::vector<parsed_turn> turns = parse_turns(convo);
		if (turns.size() >= 2) {
			const parsed_turn &prev_turn = turns[turns.size() - 2];
			const parsed_turn &curr_turn = turns[turns.size() - 1];

			if (!turbostar::context_dnn::get_instance().is_loaded()) {
				turbostar::context_dnn::get_instance().load_weights();
			}

			if (turbostar::context_dnn::get_instance().is_loaded()) {
				double active_tokens = 0.0;
				int last_boundary_idx = -1;
				for (int i = 0; i < static_cast<int>(turns.size()) - 1; ++i) {
					if (turns[i].is_boundary) {
						last_boundary_idx = i;
					}
				}
				for (int i = last_boundary_idx + 1; i < static_cast<int>(turns.size()) - 1; ++i) {
					double prev_tokens =
					    static_cast<double>(turns[i].prompt.length() + turns[i].response.length()) / 4.0;
					active_tokens += prev_tokens;
				}

				std::vector<float> M(16, 0.0f);

				double gap_sec = 0.0;
				if (curr_turn.timestamp > 0 && prev_turn.timestamp > 0) {
					gap_sec = static_cast<double>(curr_turn.timestamp) -
						  (static_cast<double>(prev_turn.timestamp) +
						   static_cast<double>(prev_turn.duration_ms) / 1000.0);
				}
				if (gap_sec < 60.0) {
					M[0] = 1.0f;
				} else if (gap_sec < 300.0) {
					M[1] = 1.0f;
				} else {
					M[2] = 1.0f;
				}

				double think_sec = static_cast<double>(prev_turn.duration_ms) / 1000.0;
				if (think_sec < 10.0) {
					M[3] = 1.0f;
				} else if (think_sec < 120.0) {
					M[4] = 1.0f;
				} else {
					M[5] = 1.0f;
				}

				double pressure_ratio = active_tokens / 8192.0;
				if (pressure_ratio < 0.60) {
					M[6] = 1.0f;
				} else if (pressure_ratio < 0.80) {
					M[7] = 1.0f;
				} else if (pressure_ratio < 0.95) {
					M[8] = 1.0f;
				} else {
					M[9] = 1.0f;
				}

				M[10] = prev_turn.git_commit ? 1.0f : 0.0f;
				M[11] = prev_turn.compile ? 1.0f : 0.0f;
				M[12] = prev_turn.test ? 1.0f : 0.0f;

				std::string text_prev = prev_turn.prompt + " [Agent Conclusion: ] " + get_last_50_words(prev_turn.response);
				std::string text_curr = curr_turn.prompt;

				auto start = std::chrono::high_resolution_clock::now();
				boundary_prob = turbostar::context_dnn::get_instance().predict_boundary(text_prev, text_curr, M);
				auto end = std::chrono::high_resolution_clock::now();
				double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

				last_boundary_prob_ = boundary_prob;
				last_inference_duration_ms_ = duration_ms;

				if (global_queue_ && boundary_prob >= 0.0f) {
					editor_event ev;
					ev.type = event_type::set_transient_status;
					ev.payload = std::format("Milestone boundary prob: {:.1f}% (latency: {:.2f} ms)",
								 boundary_prob * 100.0f, duration_ms);
					ev.priority = status_priorities::INFO;
					global_queue_->push(ev);
				}

				if (boundary_prob >= 0.0f) {
					double threshold = 0.35;
					if (model_ && model_->get_cost_type() != model_cost_type::free_local) {
						threshold = 0.65;
					}
					if (boundary_prob >= threshold) {
						should_split = true;
					}
				}
			}
		}
	}

	if (should_split) {
		std::string reason_msg;
		if (recent_chars > limit_chars) {
			reason_msg = std::format("Heuristic context character limit reached ({} tokens)", safeguard_tokens);
		} else if (boundary_prob >= 0.0f) {
			reason_msg = std::format("Milestone boundary classification trigger, prob: {:.1f}%", boundary_prob * 100.0f);
		} else {
			reason_msg = "Milestone boundary classification trigger";
		}

		event_logger::get_instance().log("Triggering auto-episode boundary split: {}", reason_msg);
		increment_stat("auto_episodes_forced");

		{
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			message marker_msg;
			marker_msg.role = "system";
			marker_msg.content =
			    "[SYSTEM MEMORY: Episode Archived]\nTitle: Auto-Episode\nSummary: " + reason_msg + "\nTags: [auto-episode]";
			conversation_.push_back(marker_msg);

			convo.push_back(marker_msg);
		}

		snapshot_episode("Auto-Episode", reason_msg, {"auto-episode"});
		add_interaction(std::make_shared<interaction_system_message>("Auto-Episode boundary inserted (" + reason_msg + ")."));
	}
}

void ai_agent::evaluate_compaction()
{
	if (!model_)
		return;

	// Calculate target bounds based on model's max_context_tokens
	int max_tokens = model_->get_max_context_tokens();

	// Default to 80% upper bound trigger, 40% lower bound target as requested
	double upper_pct = 0.8;
	double lower_pct = 0.4;

	int upper_bound = static_cast<int>(max_tokens * upper_pct);
	int lower_bound = static_cast<int>(max_tokens * lower_pct);

	int current_active_tokens = 0;
	std::map<std::string, int> active_episodes;
	std::vector<active_episode_info> candidates;

	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		std::set<std::string> accounted_episodes;

		for (const auto &msg : conversation_) {
			if (!msg.episode_id.empty() && msg.episode_level != -1 && msg.episode_level != 99) {
				active_episodes[msg.episode_id] = msg.episode_level;
				if (accounted_episodes.find(msg.episode_id) == accounted_episodes.end()) {
					accounted_episodes.insert(msg.episode_id);
					auto it = episode_index_.find(msg.episode_id);
					if (it != episode_index_.end()) {
						if (msg.episode_level == 0) {
							current_active_tokens += it->second.tokens_level_0;
						} else if (msg.episode_level == 1) {
							current_active_tokens += it->second.tokens_level_1;
						} else if (msg.episode_level == 2) {
							current_active_tokens += it->second.tokens_level_2;
						}
					}
				}
			} else {
				current_active_tokens += compaction_engine::estimate_message_tokens(msg);
			}
		}

		active_tokens_.store(current_active_tokens);

		if (current_active_tokens <= upper_bound) {
			return; // No compaction needed
		}

		// Populate candidates for compaction
		for (const auto &[ep_id, level] : active_episodes) {
			auto it = episode_index_.find(ep_id);
			if (it != episode_index_.end()) {
				candidates.push_back({ep_id, level, it->second.lru_seq, it->second.tokens_level_0,
						      it->second.tokens_level_1, it->second.tokens_level_2});
			}
		}
	}

	event_logger::get_instance().log("Active history tokens ({}) exceed upper bound ({}). Triggering compaction engine.",
					 current_active_tokens, upper_bound);

	// Run decision engine to plan transitions (outside lock to avoid recursive deadlocks in set_episode_state)
	std::vector<transition> planned = compaction_engine::plan_compaction(candidates, current_active_tokens, lower_bound);

	if (planned.empty()) {
		event_logger::get_instance().log("Compaction engine: No active episodes eligible for compaction.");
		return;
	}

	// Apply planned transitions
	for (const auto &trans : planned) {
		event_logger::get_instance().log("Compaction engine: Auto-shifting {} to level {}", trans.episode_id, trans.target_level);
		set_episode_state(trans.episode_id, trans.target_level);
	}
}

void ai_agent::update_episode_hint(const std::string &episode_id, const std::string &hint)
{
	// Update the index memory
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		if (episode_index_.find(episode_id) != episode_index_.end()) {
			episode_index_[episode_id].reactivation_hint = hint;
		}

		// Mutate the active conversation
		for (auto &msg : conversation_) {
			if (msg.role == "system" && msg.content.find(episode_id) != std::string::npos) {
				// We found the marker, append the hint
				msg.content += "\nDemand-Load Hint: " + hint;
				break;
			}
		}
	}

	// Rewrite the metadata sidecar
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";
	if (std::filesystem::exists(meta_filepath)) {
		try {
			std::ifstream file(meta_filepath);
			nlohmann::json root;
			file >> root;
			root["reactivation_hint"] = hint;
			std::ofstream out(meta_filepath);
			out << root.dump(4);
		} catch (...) {
		}
	}
}

void ai_agent::summary_worker_loop()
{
	event_logger::get_instance().log("Thread started: ai_agent summary worker");

	while (!is_closed_ && !project_manager::get_instance().is_exiting()) {
		pending_summary task;
		{
			std::unique_lock<std::mutex> lock(summary_mutex_);
			summary_cv_.wait(
			    lock, [this] { return is_closed_ || project_manager::get_instance().is_exiting() || !summary_queue_.empty(); });

			if (is_closed_ || project_manager::get_instance().is_exiting())
				break;
			if (summary_queue_.empty())
				continue;

			task = summary_queue_.front();
			summary_queue_.erase(summary_queue_.begin());
		}

		try {
			if (is_closed_ || project_manager::get_instance().is_exiting())
				break;
			std::ifstream file(task.filepath);
			if (!file.is_open())
				continue;

			nlohmann::json root;
			file >> root;

			if (root.contains("conversation") && root["conversation"].is_array()) {
				std::string context_dump = root["conversation"].dump(2);

				if (context_dump.length() < 1000) {
					update_episode_hint(task.episode_id, "Trivial or extremely brief episode.");
					continue;
				}

				std::string system_prompt =
				    "You are an AI context-management assistant on a strict token budget. Below is an archived "
				    "conversation 'episode' between a software engineer and an AI agent. "
				    "Write an ultra-terse 'demand-load hint' (max 1-2 sentences) so future AI agents know WHEN to retrieve "
				    "this episode into their active memory. "
				    "Focus ONLY on the specific technical problems solved, files modified, and decisions made. Use highly "
				    "compressed, telegraphic language to save tokens. "
				    "Start your response exactly with 'Reactivate when:' and do NOT use any conversational filler.\n\n"
				    "EPISODE JSON:\n" +
				    context_dump;

				auto default_model = ai_model_registry::get_instance().get_default_model();
				if (!default_model) {
					default_model = model_;
				}

				size_t max_chars =
				    default_model ? static_cast<size_t>(default_model->get_max_context_tokens() * 4) : 250000;

				if (system_prompt.length() > max_chars) {
					std::string fallback_hint =
					    "Reactivate when: Large episode. Title: " + root.value("title", "Untitled") +
					    ". Summary: " + root.value("summary", "No summary provided.");
					if (fallback_hint.length() > 500) {
						fallback_hint = fallback_hint.substr(0, 500) + "...";
					}
					update_episode_hint(task.episode_id, fallback_hint);
					event_logger::get_instance().log("Skipped background LLM summary for {} because size ({}) exceeds "
									 "context limit ({}). Used fallback hint.",
									 task.episode_id, system_prompt.length(), max_chars);
					continue;
				}

				std::vector<message> dummy_convo;
				message sys;
				sys.role = "system";
				sys.content = system_prompt;
				dummy_convo.push_back(sys);

				auto transport =
				    std::make_shared<httplib_transport>(default_model->get_url(), default_model->get_api_key());
				llm_client local_client(transport, default_model->get_id(), default_model->get_api_type());

				bool should_break = false;
				{
					std::lock_guard<std::mutex> lock(background_transport_mutex_);
					if (is_closed_ || project_manager::get_instance().is_exiting()) {
						should_break = true;
					} else {
						background_transport_ = transport;
					}
				}

				if (should_break) {
					break;
				}

				llm_chat_response res = local_client.send_chat(dummy_convo);

				{
					std::lock_guard<std::mutex> lock(background_transport_mutex_);
					background_transport_.reset();
				}

				if (is_closed_ || project_manager::get_instance().is_exiting())
					break;
				if (!res.msg.content.empty()) {
					bool is_error = res.msg.content.starts_with("Error connecting to LLM server") ||
							res.msg.content.starts_with("Error parsing response JSON");
					if (!is_error) {
						update_episode_hint(task.episode_id, res.msg.content);
						event_logger::get_instance().log("Generated background summary for {}", task.episode_id);
					} else {
						event_logger::get_instance().log("Skipped saving background summary due to LLM error: {}",
										 res.msg.content);
					}
				}
			}
		} catch (const std::exception &e) {
			event_logger::get_instance().log("Error in background summarization: {}", std::string(e.what()));
		}
	}
	event_logger::get_instance().log("Thread exited: ai_agent summary worker");
}

std::vector<compaction_segment> ai_agent::get_compaction_segments() const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	std::vector<compaction_segment> segments;

	std::string current_ep_id = "";
	compaction_segment current_seg;
	bool in_episode = false;

	for (const auto &msg : conversation_) {
		std::string ep_id = msg.episode_id;
		int ep_level = msg.episode_level;

		if (ep_id.empty() && msg.role == "system" && msg.content.find("[SYSTEM MEMORY: Episode Archived]") != std::string::npos) {
			size_t arch_pos = msg.content.find("Raw history archive: ");
			if (arch_pos != std::string::npos) {
				ep_id = msg.content.substr(arch_pos + 21);
				while (!ep_id.empty() &&
				       (ep_id.back() == '\r' || ep_id.back() == '\n' || ep_id.back() == ' ' || ep_id.back() == '\t')) {
					ep_id.pop_back();
				}
				ep_level = 99;
			}
		}

		if (!ep_id.empty()) {
			if (in_episode && ep_id == current_ep_id) {
				if (ep_level != -1) {
					current_seg.current_level = ep_level;
				}
			} else {
				if (!current_ep_id.empty() || in_episode) {
					segments.push_back(current_seg);
				}
				current_ep_id = ep_id;
				in_episode = true;
				current_seg.label = "";
				current_seg.uncompacted_tokens = 0;
				current_seg.current_level = (ep_level != -1) ? ep_level : 0;

				auto it = episode_index_.find(current_ep_id);
				if (it != episode_index_.end()) {
					current_seg.label = it->second.title;
					current_seg.uncompacted_tokens = it->second.tokens_level_0;
				} else {
					current_seg.label = current_ep_id;
					current_seg.uncompacted_tokens = 0;
				}
			}
		} else {
			int msg_tokens = compaction_engine::estimate_message_tokens(msg);
			if (!in_episode && !segments.empty() && segments.back().label == "Recent") {
				segments.back().uncompacted_tokens += msg_tokens;
			} else {
				if (in_episode || !current_ep_id.empty()) {
					segments.push_back(current_seg);
					in_episode = false;
					current_ep_id = "";
				}
				if (!segments.empty() && segments.back().label == "Recent") {
					segments.back().uncompacted_tokens += msg_tokens;
				} else {
					compaction_segment recent_seg;
					recent_seg.label = "Recent";
					recent_seg.uncompacted_tokens = msg_tokens;
					recent_seg.current_level = 0;
					segments.push_back(recent_seg);
				}
			}
		}
	}
	if (in_episode || !current_ep_id.empty()) {
		segments.push_back(current_seg);
	}

	return segments;
}

void ai_agent::inject_archived_episodes_summary()
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (episode_index_.empty())
		return;

	std::vector<const episode_index_entry *> sorted;
	for (const auto &pair : episode_index_) {
		sorted.push_back(&pair.second);
	}
	std::sort(sorted.begin(), sorted.end(),
		  [](const episode_index_entry *a, const episode_index_entry *b) { return a->episode_seq < b->episode_seq; });

	std::stringstream oss;
	oss << "[SYSTEM MEMORY: Archived Episodes Directory]\n";
	oss << "The following past episodes have been paged out to disk to save context budget:\n\n";
	oss << "| Episode | When to Resume |\n";
	oss << "|---|---|\n";
	bool has_any = false;
	for (const auto *mi : sorted) {
		if (mi->reactivation_hint.find("Trivial or extremely brief") != std::string::npos) {
			continue;
		}
		std::string hint = mi->reactivation_hint;
		if (hint.empty()) {
			hint = "(No reactivation hint available)";
		}
		oss << "| " << mi->id << " | " << hint << " |\n";
		has_any = true;
	}
	if (!has_any)
		return;

	oss << "\nIf you need to retrieve the detailed history or code changes from any of these past episodes, you can restore them into "
	       "your active context by calling the `agent_restore_context` tool with the corresponding Episode ID (e.g. "
	       "`agent_restore_context(\"episode_1\", 1)`).";

	message msg;
	msg.role = "system";
	msg.content = oss.str();

	// Insert this message right after the initial system prompt (which is at index 0)
	if (conversation_.empty()) {
		conversation_.push_back(msg);
	} else {
		conversation_.insert(conversation_.begin() + 1, msg);
	}
}

void ai_agent::set_final_result(const std::string &result)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	final_result_ = result;
}

std::string ai_agent::get_final_result() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return final_result_;
}

bool ai_agent::has_final_result() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return !final_result_.empty();
}

} // namespace agentlib
