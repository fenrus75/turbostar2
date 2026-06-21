#include "ai_agent.h"
#include "agentlib/tool_registry.h"
#include <algorithm>
#include <chrono>
#include <iostream>
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
#include "../mcp/mcp_manager.h"
#include "../project_manager.h"
#include "compaction_engine.h"
#include "context_dnn.h"
#include "copilot_manager.h"
#include "httplib_transport.h"
#include "skill_manager.h"
#include "data/conversation.h"
#include "data/system_turn.h"
#include "data/user_turn.h"
#include "data/model_response_turn.h"
#include "data/tool_execution_turn.h"
#include "data/error_turn.h"

namespace agentlib
{

void to_json(nlohmann::json &j, const todo_item &todo)
{
	j = nlohmann::json{{"text", todo.text}, {"completed", todo.completed}, {"reminder_count", todo.reminder_count}};
}

void from_json(const nlohmann::json &j, todo_item &todo)
{
	j.at("text").get_to(todo.text);
	j.at("completed").get_to(todo.completed);
	if (j.contains("reminder_count")) {
		j.at("reminder_count").get_to(todo.reminder_count);
	} else {
		todo.reminder_count = 0;
	}
}

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
		case agent_status::dead:
			return "Dead";
		default:
			return "Unknown";
	}
}

std::string agent_status_to_name(agent_status status)
{
	switch (status) {
		case agent_status::idle:
			return "Idle";
		case agent_status::thinking:
			return "Thinking";
		case agent_status::tool_execution:
			return "Tool Execution";
		case agent_status::waiting:
			return "Waiting";
		case agent_status::error:
			return "Error";
		case agent_status::dead:
			return "Dead";
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

bool ai_agent::is_mutation_possible() const
{
	if (!model_)
		return true;
	return model_->get_api_type() != api_type::openai_response;
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
	if (!conversation_) return 0;
	return conversation_->estimate_token_count();
}

std::vector<std::string> ai_agent::page_in_history_auto(int default_level, double target_fraction)
{
	if (!is_mutation_possible())
		return {};

	std::vector<const episode_index_entry *> paged_out;
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		for (const auto &pair : episode_index_) {
			bool is_paged_out = true;
			for (const auto &ep : conversation_->get_episodes()) {
				if (ep->get_id() == pair.first && ep->get_compaction_level() != COMPACTION_LEVEL_PAGED_OUT) {
					is_paged_out = false;
					break;
				}
			}
			if (is_paged_out) {
				paged_out.push_back(&pair.second);
			}
		}
	}

	std::sort(paged_out.begin(), paged_out.end(),
		  [](const episode_index_entry *a, const episode_index_entry *b) { return a->episode_seq > b->episode_seq; });

	int current_tokens = calculate_current_tokens();
	int max_tokens = model_ ? model_->get_max_context_tokens() : 250000;
	int limit_tokens = static_cast<int>(max_tokens * target_fraction);
	std::cout << "[debug page_in_history_auto] current_tokens=" << current_tokens 
	          << ", max_tokens=" << max_tokens << ", limit_tokens=" << limit_tokens 
	          << ", paged_out count=" << paged_out.size() << std::endl;

	std::vector<std::string> paged_in_ids;

	for (const auto *entry : paged_out) {
		int ep_tokens = 0;
		if (default_level == 0)
			ep_tokens = entry->tokens_level_0;
		else if (default_level == 1)
			ep_tokens = entry->tokens_level_1;
		else if (default_level == 2)
			ep_tokens = entry->tokens_level_2;
		if (ep_tokens <= 0)
			ep_tokens = entry->tokens_level_0;

		int old_tokens = static_cast<int>((entry->title.size() + entry->summary.size()) / 4 + 50);
		int net_change = ep_tokens - old_tokens;
		if (net_change < 0) net_change = 0;

		std::cout << "  [debug page_in_history_auto] entry=" << entry->id << ", ep_tokens=" << ep_tokens 
		          << ", old_tokens=" << old_tokens << ", net_change=" << net_change 
		          << ", sum=" << (current_tokens + net_change) << std::endl;
		if (current_tokens + net_change <= limit_tokens) {
			if (set_episode_state(entry->id, default_level)) {
				current_tokens += net_change;
				paged_in_ids.push_back(entry->id);
			}
		} else {
			break;
		}
	}

	active_tokens_.store(current_tokens);
	return paged_in_ids;
}

bool ai_agent::set_episode_state(const std::string &episode_id, int target_level)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	
	// Find in loaded episodes
	for (const auto &ep : conversation_->get_episodes()) {
		if (ep->get_id() == episode_id) {
			/*
			 * When transitioning a paged-out episode (compaction level 99) to a paged-in state
			 * (compaction level < 99), load the actual transaction history from its JSON storage.
			 * When paging out, flush the current transactions to disk and clear the in-memory details
			 * to minimize active memory usage.
			 */
			if (ep->get_compaction_level() == COMPACTION_LEVEL_PAGED_OUT && target_level < COMPACTION_LEVEL_PAGED_OUT) {
				std::string history_dir = fs_utils::get_project_history_dir(name_);
				std::string filepath = history_dir + "/" + episode_id + ".json";
				if (std::filesystem::exists(filepath)) {
					try {
						std::ifstream file(filepath);
						nlohmann::json root;
						file >> root;
						auto disk_ep = Episode::deserialize(root);
						ep->copy_from(*disk_ep);
					} catch (...) {}
				}
			} else if (ep->get_compaction_level() < COMPACTION_LEVEL_PAGED_OUT && target_level == COMPACTION_LEVEL_PAGED_OUT) {
				if (!ep->is_finalized()) {
					std::string history_dir = fs_utils::get_project_history_dir(name_);
					std::string filepath = history_dir + "/" + episode_id + ".json";
					try {
						ep->set_finalized(true);
						nlohmann::json root = ep->serialize();
						std::ofstream file(filepath);
						if (file.is_open()) {
							file << root.dump(4);
						}
					} catch (...) {}
				}
				ep->clear_transactions();
			}

			ep->set_compaction_level(target_level);
			long long l_seq = next_lru_seq_++;
			if (episode_index_.find(episode_id) != episode_index_.end()) {
				episode_index_[episode_id].lru_seq = l_seq;
			}
			
			// Update LRU seq in meta file on disk
			std::string history_dir = fs_utils::get_project_history_dir(name_);
			std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";
			try {
				std::ifstream mfile(meta_filepath);
				nlohmann::json meta_root;
				mfile >> meta_root;
				meta_root["lru_seq"] = l_seq;
				std::ofstream mfile_out(meta_filepath);
				mfile_out << meta_root.dump(4);
			} catch (...) {}

			if (global_queue_) {
				editor_event ev;
				ev.type = event_type::agent_tool_update;
				ev.key_code = id_;
				global_queue_->push(ev);
			}
			return true;
		}
	}
	
	// If not found in memory but exists in metadata, we load it from disk
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	if (std::filesystem::exists(filepath)) {
		try {
			std::ifstream file(filepath);
			nlohmann::json root;
			file >> root;
			auto ep = Episode::deserialize(root);
			ep->set_compaction_level(target_level);
			conversation_->add_episode(ep);
			
			long long l_seq = next_lru_seq_++;
			if (episode_index_.find(episode_id) != episode_index_.end()) {
				episode_index_[episode_id].lru_seq = l_seq;
			}
			
			std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";
			try {
				std::ifstream mfile(meta_filepath);
				nlohmann::json meta_root;
				mfile >> meta_root;
				meta_root["lru_seq"] = l_seq;
				std::ofstream mfile_out(meta_filepath);
				mfile_out << meta_root.dump(4);
			} catch (...) {}

			if (global_queue_) {
				editor_event ev;
				ev.type = event_type::agent_tool_update;
				ev.key_code = id_;
				global_queue_->push(ev);
			}
			return true;
		} catch (...) {}
	}

	return false;
}


ai_agent::ai_agent(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue, document_provider *doc_provider)
    : id_(id), name_(name), model_(std::move(model)), global_queue_(queue), doc_provider_(doc_provider)
{
	conversation_ = std::make_shared<Conversation>();
	if (model_) {
		conversation_->set_model(model_);
	}
	auto http_transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
	if (model_->get_api_type() == api_type::copilot) {
		http_transport->set_token_provider([]() { return copilot_manager::get_instance().get_copilot_token(); });
	}
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
	save_todos_internal();
}

void ai_agent::save_todos_internal() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	save_todos_internal_unlocked();
}

void ai_agent::save_todos_internal_unlocked() const
{
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/todos.json";
	try {
		nlohmann::json todo_root = nlohmann::json::array();
		for (const auto &todo : todos_) {
			nlohmann::json item;
			to_json(item, todo);
			todo_root.push_back(item);
		}
		std::ofstream todo_file(filepath);
		if (todo_file.is_open()) {
			todo_file << todo_root.dump(4);
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to save todos.json: {}", std::string(e.what()));
	}
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

	// Load todos from todos.json if it exists
	std::string todo_filepath = history_dir + "/todos.json";
	if (std::filesystem::exists(todo_filepath)) {
		try {
			std::ifstream todo_file(todo_filepath);
			nlohmann::json todo_root;
			todo_file >> todo_root;
			if (todo_root.is_array()) {
				std::lock_guard<std::mutex> state_lock(state_mutex_);
				todos_.clear();
				for (const auto &item : todo_root) {
					todo_item todo;
					from_json(item, todo);
					todos_.push_back(todo);
				}
			}
		} catch (const std::exception &e) {
			event_logger::get_instance().log("Failed to load todos.json: {}", std::string(e.what()));
		}
	}

	try {
		std::ifstream file(filepath);
		nlohmann::json root;
		file >> root;
		if (root.contains("final_result") && root["final_result"].is_string()) {
			std::lock_guard<std::mutex> state_lock(const_cast<std::mutex &>(state_mutex_));
			final_result_ = root["final_result"].get<std::string>();
		}

		if (root.contains("conversation")) {
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			if (root["conversation"].is_object()) {
				conversation_ = Conversation::deserialize(root["conversation"]);
			} else if (root["conversation"].is_array()) {
				std::vector<message> old_convo;
				for (const auto &item : root["conversation"]) {
					message msg;
					from_json(item, msg);
					old_convo.push_back(msg);
				}
				set_conversation_unlocked(old_convo);
			}
			
			if (model_ && !conversation_->get_model()) {
				conversation_->set_model(model_);
			}

			// Detect and normalize tool call sequences (reorder tool responses and abort pending tool calls).
			auto flat_convo = get_conversation_unlocked();
			std::vector<message> normalized_convo;
			std::map<std::string, message> tool_responses;

			// 1. Extract all tool responses
			for (const auto &msg : flat_convo) {
				if (msg.role == "tool" && msg.tool_call_id) {
					tool_responses[*msg.tool_call_id] = msg;
				}
			}

			// 2. Reconstruct the conversation in the correct order
			bool needs_update = false;
			for (const auto &msg : flat_convo) {
				if (msg.role == "tool") {
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
							abort_msg.content = "Tool execution aborted: Editor session was restarted before completion.";
							normalized_convo.push_back(abort_msg);
							needs_update = true;

							event_logger::get_instance().log("Aborted pending tool call: " + tc.id + " (" + tc.function.name + ")");
						}
					}
				}
			}

			// 3. Discard any orphan tool responses
			if (!tool_responses.empty()) {
				event_logger::get_instance().log("Discarded " + std::to_string(tool_responses.size()) +
					" orphaned tool response(s) with no matching assistant tool call in active context.");
				needs_update = true;
			}

			if (normalized_convo.size() != flat_convo.size()) {
				needs_update = true;
			} else {
				for (size_t i = 0; i < flat_convo.size(); ++i) {
					if (flat_convo[i].role != normalized_convo[i].role ||
					    flat_convo[i].content != normalized_convo[i].content ||
					    flat_convo[i].tool_call_id != normalized_convo[i].tool_call_id) {
						needs_update = true;
						break;
					}
				}
			}

			if (needs_update) {
				set_conversation_unlocked(normalized_convo);
			}
		}

		// Page in recent episodes up to 30% target fraction on startup
		page_in_history_auto(1, 0.3);

		event_logger::get_instance().log("Agent {} restored active state from {}", name_, filepath);
		return true;
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to restore active state: {}", std::string(e.what()));
	}
	return false;
}

void ai_agent::close()
{
	if (!is_closed_) {
		if (is_mutation_possible()) {
			// Implicit Episode: Page out all uncompressed history when the editor closes
			// so the agent boots up "fresh" (but with pointers) next session.
			page_out_prior_context("", true, "End of Session",
					       "The user closed the editor or agent window. This session was automatically paged out.",
					       {"session-end"});
		}

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
	save_todos_internal_unlocked();
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
	save_todos_internal_unlocked();

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
		std::lock_guard<std::mutex> lock(properties_mutex_);
		if (std::find(properties_.active_families.begin(), properties_.active_families.end(), family_name) == properties_.active_families.end()) {
			properties_.active_families.push_back(family_name);
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
		std::lock_guard<std::mutex> lock(properties_mutex_);
		for (const auto &fam : properties_.active_families) {
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
		std::lock_guard<std::mutex> lock(properties_mutex_);
		if (std::find(properties_.active_families.begin(), properties_.active_families.end(), family_name) != properties_.active_families.end()) {
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
	if (!conversation_) {
		return;
	}

	std::shared_ptr<system_turn> first_sys = nullptr;
	if (auto curr_ep = conversation_->get_current_episode()) {
		for (const auto &tx : curr_ep->get_transactions()) {
			for (const auto &turn : tx->get_turns()) {
				if (turn->get_type() == turn_type::system) {
					first_sys = std::dynamic_pointer_cast<system_turn>(turn);
					if (first_sys) break;
				}
			}
			if (first_sys) break;
		}
	}
	if (!first_sys) {
		for (const auto &ep : conversation_->get_episodes()) {
			for (const auto &tx : ep->get_transactions()) {
				for (const auto &turn : tx->get_turns()) {
					if (turn->get_type() == turn_type::system) {
						first_sys = std::dynamic_pointer_cast<system_turn>(turn);
						if (first_sys) break;
					}
				}
				if (first_sys) break;
			}
			if (first_sys) break;
		}
	}

	if (first_sys) {
		if (original_system_prompt_.empty()) {
			original_system_prompt_ = first_sys->get_content();
		}

		// Rebuild the system prompt content
		std::string families_str;
		auto families = get_active_tool_families();
		bool first_fam = true;
		for (const auto &fam : families) {
			if (fam.starts_with(':')) {
				continue;
			}
			if (!first_fam) {
				families_str += ", ";
			}
			families_str += std::format("'{}'", fam);
			first_fam = false;
		}

		std::string table_str;
		auto registered_families = tool_registry::get_instance().get_all_registered_families();
		std::vector<std::string> inactive_families;
		for (const auto &fam : registered_families) {
			if (fam.starts_with(':')) {
				continue;
			}
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
				std::string reason = tool_registry::get_instance().get_tool_family_reason(fam);
				if (reason.empty()) {
					std::string cached =
					    config_manager::get_instance().get_mcp_server_when_to_activate(fam, false);
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

		std::string new_content = std::format("{}\n\n*** ACTIVE TOOL FAMILIES ***\n"
					  "The following tool families are currently active and available: [{}].{}",
					  original_system_prompt_, families_str, table_str);
		first_sys->set_content(new_content);
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
		save_todos_internal_unlocked();
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
	save_todos_internal_unlocked();
	return true;
}

bool ai_agent::delete_todo(const std::string &text_match, std::string &out_error)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (text_match == "*") {
		todos_.clear();
		save_todos_internal_unlocked();
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
	save_todos_internal_unlocked();
	return true;
}

std::string ai_agent::get_todo_reminder_msg()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	int outstanding_count = 0;
	todo_item *next_todo = nullptr;

	for (auto &todo : todos_) {
		if (!todo.completed) {
			outstanding_count++;
			if (!next_todo) {
				next_todo = &todo;
			}
		}
	}

	if (outstanding_count == 0) {
		return "";
	}

	std::string msg;
	if (outstanding_count == 1) {
		msg = "1 todo item remaining.";
	} else {
		msg = std::format("{} todo items remaining.", outstanding_count);
	}

	if (next_todo) {
		if (next_todo->reminder_count < 2) {
			msg += std::format(" Next todo item: '{}'", next_todo->text);
			next_todo->reminder_count++;
			save_todos_internal_unlocked();
		}
	}

	return msg;
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

std::vector<std::shared_ptr<agent_interaction>> ai_agent::get_interactions() const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	std::vector<std::shared_ptr<agent_interaction>> res;
	if (!conversation_)
		return res;
	for (const auto &ep : conversation_->get_episodes()) {
		for (const auto &tx : ep->get_transactions()) {
			for (const auto &turn : tx->get_turns()) {
				if (turn->get_type() == turn_type::tool_execution) {
					if (auto tool_turn = dynamic_cast<const tool_execution_turn *>(turn.get())) {
						for (const auto &inter : tool_turn->get_tool_interactions()) {
							res.push_back(inter);
						}
					}
				} else if (auto inter = turn->get_interaction()) {
					res.push_back(inter);
				}
			}
		}
	}
	if (auto curr_ep = conversation_->get_current_episode()) {
		for (const auto &tx : curr_ep->get_transactions()) {
			for (const auto &turn : tx->get_turns()) {
				if (turn->get_type() == turn_type::tool_execution) {
					if (auto tool_turn = dynamic_cast<const tool_execution_turn *>(turn.get())) {
						for (const auto &inter : tool_turn->get_tool_interactions()) {
							res.push_back(inter);
						}
					}
				} else if (auto inter = turn->get_interaction()) {
					res.push_back(inter);
				}
			}
		}
	}
	return res;
}

void ai_agent::add_interaction(std::shared_ptr<agent_interaction> interaction)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (!conversation_)
		return;

	// Update ages of all existing interactions in the conversation
	for (const auto &ep : conversation_->get_episodes()) {
		for (const auto &tx : ep->get_transactions()) {
			for (const auto &turn : tx->get_turns()) {
				if (auto inter = turn->get_interaction()) {
					inter->set_age(inter->get_age() + 1);
				}
			}
		}
	}
	if (auto curr_ep = conversation_->get_current_episode()) {
		for (const auto &tx : curr_ep->get_transactions()) {
			for (const auto &turn : tx->get_turns()) {
				if (auto inter = turn->get_interaction()) {
					inter->set_age(inter->get_age() + 1);
				}
			}
		}
	}
	interaction->set_age(0);

	auto curr_ep = conversation_->get_current_episode();
	if (!curr_ep) {
		long long seq = conversation_->allocate_next_episode_seq();
		curr_ep = conversation_->create_new_episode("episode_" + std::to_string(seq), "Active Session", "");
		curr_ep->set_sequence_number(seq);
	}

	std::string tx_id = "tx_ui_notif_" + std::to_string(std::rand()) + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	auto tx = std::make_shared<Transaction>(tx_id, transaction_type::ui_notification);

	std::string turn_id = "turn_ui_notif_" + std::to_string(std::rand());
	auto turn = std::make_shared<system_turn>(turn_id, interaction->get_raw_text(), "ui_notification");
	turn->set_interaction(interaction);
	turn->set_sequence_number(conversation_->allocate_next_turn_seq());
	tx->add_turn(turn);

	curr_ep->add_transaction(tx);
}

void ai_agent::inject_context(const std::string &role, const std::string &content, bool trigger_processing)
{
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		if (conversation_) {
			auto ep = conversation_->get_current_episode();
			if (!ep) {
				long long seq = conversation_->allocate_next_episode_seq();
				std::string ep_id = "episode_" + std::to_string(seq);
				ep = conversation_->create_new_episode(ep_id, "Injected Context", "System injected context");
				ep->set_sequence_number(seq);
			}

			bool merged = false;
			// If the newly injected context is a system message, we check if the last transaction
			// was also a system injection. If so, we merge the new content into the existing
			// system turn rather than creating a new transaction. This keeps the stored history
			// consolidated, ensures a cleaner UI display, and avoids protocol/API fragmentation.
			if (role == "system" && ep && !ep->get_transactions().empty()) {
				auto last_tx = ep->get_transactions().back();
				if (last_tx->get_type() == transaction_type::system_injection && !last_tx->get_turns().empty()) {
					auto last_turn = last_tx->get_turns().back();
					if (last_turn->get_type() == turn_type::system) {
						std::string new_content = last_turn->get_content();
						if (!new_content.empty()) {
							new_content += "\n\n";
						}
						new_content += content;
						last_turn->set_content(new_content);
						if (auto inter = last_turn->get_interaction()) {
							inter->push_content(new_content);
						}
						// If this turn is the first system turn (which caches its original prompt),
						// we must also update original_system_prompt_ so that subsequent 
						// update_system_prompt_with_families() calls do not overwrite/discard 
						// the merged content.
						if (!original_system_prompt_.empty()) {
							original_system_prompt_ += "\n\n" + content;
						}
						merged = true;
					}
				}
			}
			
			if (!merged) {
				std::string tx_id = "tx_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + std::to_string(std::rand() % 1000);
				transaction_type tx_type = transaction_type::system_injection;
				if (role == "user" || role == "assistant") {
					tx_type = transaction_type::user_exchange;
				}
				auto tx = std::make_shared<Transaction>(tx_id, tx_type);
				
				std::string turn_id = "turn_" + std::to_string(std::rand());
				std::shared_ptr<Turn> t;
				std::shared_ptr<agent_interaction> inter;
				if (role == "system") {
					t = std::make_shared<system_turn>(turn_id, content, "context_injection");
					inter = std::make_shared<interaction_system_message>(content);
				} else if (role == "assistant") {
					t = std::make_shared<model_response_turn>(turn_id, content, std::nullopt, std::vector<tool_call>{});
					inter = std::make_shared<interaction_llm_response>(content);
				} else {
					t = std::make_shared<user_turn>(turn_id, content);
					inter = std::make_shared<interaction_user_message>(content);
				}
				t->set_interaction(inter);
				t->set_sequence_number(conversation_->allocate_next_turn_seq());
				tx->add_turn(t);
				conversation_->add_transaction(tx);
			}
		}
	}

	if (role == "system") {
		update_system_prompt_with_families();
	}

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
		if (conversation_) {
			conversation_->set_model(model_);
		}
		auto http_transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
		if (model_->get_api_type() == api_type::copilot) {
			http_transport->set_token_provider([]() { return copilot_manager::get_instance().get_copilot_token(); });
		}
		client_ = std::make_unique<llm_client>(http_transport, model_->get_id(), model_->get_api_type());
	}

	add_interaction(
	    std::make_shared<interaction_system_message>("Model switched to: " + model_->get_name() + " (" + model_->get_id() + ")"));
}

void ai_agent::submit_prompt(const std::string &prompt_text)
{
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		if (conversation_) {
			auto ep = conversation_->get_current_episode();
			if (!ep) {
				std::string ep_id = "episode_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
				ep = conversation_->create_new_episode(ep_id, "Default Episode", "Active session episode");
			}
			
			std::string tx_id = "tx_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + std::to_string(std::rand() % 1000);
			auto tx = std::make_shared<Transaction>(tx_id, transaction_type::user_exchange);
			
			std::string turn_id = "turn_" + std::to_string(std::rand());
			auto t = std::make_shared<user_turn>(turn_id, prompt_text);
			auto inter = std::make_shared<interaction_user_message>(prompt_text);
			t->set_interaction(inter);
			t->set_sequence_number(conversation_->allocate_next_turn_seq());
			tx->add_turn(t);
			conversation_->add_transaction(tx);
		}
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
		std::vector<message> convo;

		auto &registry = tool_registry::get_instance();
		tool_context ctx;

		std::filesystem::path workspace_root(project_manager::get_instance().get_project_root());
		if (workspace_root.empty() || !std::filesystem::exists(workspace_root)) {
			workspace_root = std::filesystem::current_path();
		}

		ctx.fs_security.set_working_directory(workspace_root);
		ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
		{
			std::string allowed_write = self->get_allowed_write_file();
			if (!allowed_write.empty()) {
				std::filesystem::path allowed_file = workspace_root / allowed_write;
				ctx.fs_security.add_allowed_file(allowed_file, access_type::write);
			}
			if (!self->is_read_only()) {
				ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
			}
		}
		ctx.fs_security.set_vfs(skill_manager::get_instance().get_vfs());
		ctx.doc_provider = self->doc_provider_;
		ctx.queue = self->global_queue_;
		ctx.active_agent = self.get();
		ctx.properties = self->get_properties();
		ctx.mutation_possible = self->is_mutation_possible();
		ctx.is_family_active = [self](const std::string &family) { return self->is_tool_family_active(family); };

		std::string final_response;

		while (true) {
			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed early]", self->id_);
				return;
			}

			self->evaluate_compaction();
			std::vector<message> convo = self->get_conversation();
			self->evaluate_auto_episode(convo);

			std::string previous_response_id;
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				previous_response_id = self->last_response_id_;
				convo = self->get_conversation_unlocked();
			}

			self->set_status(agent_status::thinking);

			std::shared_ptr<Transaction> active_tx = nullptr;
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				if (self->conversation_->get_current_episode() && 
					!self->conversation_->get_current_episode()->get_transactions().empty()) {
					active_tx = self->conversation_->get_current_episode()->get_transactions().back();
				}
			}
			if (!active_tx) {
				std::string tx_id = "tx_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + std::to_string(std::rand() % 1000);
				active_tx = std::make_shared<Transaction>(tx_id, transaction_type::user_exchange);
				self->conversation_->add_transaction(active_tx);
			}

			std::string turn_id = "turn_" + std::to_string(std::rand());
			auto response_turn = std::make_shared<model_response_turn>(turn_id, "", std::nullopt, std::vector<tool_call>{});
			response_turn->set_sequence_number(self->conversation_->allocate_next_turn_seq());
			active_tx->add_turn(response_turn);

			std::shared_ptr<interaction_reasoning> current_reasoning = nullptr;
			std::shared_ptr<interaction_llm_response> current_response = nullptr;
			std::vector<tool_call> accumulated_tool_calls;

			auto props = self->get_properties();
			props.active_families = self->get_active_tool_families();

			self->client_->send_chat_stream(
			    convo,
			    [&](const chat_delta &delta) {
				    if (self->is_closed_)
					    return;

				    self->update_last_activity_time();

				    if (!delta.response_id.empty()) {
					    std::lock_guard<std::mutex> lock(self->conversation_mutex_);
					    self->last_response_id_ = delta.response_id;
				    }

				    if (!delta.reasoning_content.empty()) {
					    if (!current_reasoning) {
						    current_reasoning = std::make_shared<interaction_reasoning>("");
						    response_turn->set_reasoning_interaction(current_reasoning);
					    }
					    response_turn->append_reasoning_content(delta.reasoning_content);
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
						    response_turn->set_interaction(current_response);
					    }
					    response_turn->append_content(delta.content);
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
			    &registry, previous_response_id, props);

			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed early]", self->id_);
				return;
			}

			if (!accumulated_tool_calls.empty()) {
				for (auto &call : accumulated_tool_calls) {
					normalize_tool_call(call);
				}
				response_turn->set_tool_calls(accumulated_tool_calls);
			}

			// Complete the response turn's time range
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				time_range r = response_turn->get_time_range();
				r.end_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				response_turn->set_time_range(r);
			}

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
						this_ptr->update_last_activity_time();
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
					std::shared_ptr<tool_execution_turn> tool_turn = nullptr;
					{
						std::lock_guard<std::mutex> lock(self->conversation_mutex_);
						if (!active_tx->get_turns().empty()) {
							auto last_t = active_tx->get_turns().back();
							if (last_t->get_type() == turn_type::tool_execution) {
								tool_turn = std::dynamic_pointer_cast<tool_execution_turn>(last_t);
							}
						}
						if (!tool_turn) {
							std::string tool_turn_id = "turn_" + std::to_string(std::rand());
							tool_turn = std::make_shared<tool_execution_turn>(tool_turn_id);
							tool_turn->set_sequence_number(self->conversation_->allocate_next_turn_seq());
							active_tx->add_turn(tool_turn);
						}
					}

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
							tool_turn->add_tool_interaction(std::make_shared<interaction_tool_call>(
							    call.function.name,
							    std::format("{}(merged into {})", call.function.name, parent_id)));
							if (self->global_queue_) {
								editor_event tool_ev;
								tool_ev.type = event_type::agent_tool_update;
								tool_ev.key_code = self->id_;
								self->global_queue_->push(tool_ev);
							}
						}
					} else {
						ctx.properties = self->get_properties();
						ctx.tool_call_id = call.id;
						auto prep = registry.prepare_tool(call.function.name, call.function.arguments, ctx);

						if (prep.tool) {
							custom_interaction = prep.tool->get_interaction();
						}

						if (!is_silent) {
							if (custom_interaction) {
								tool_turn->add_tool_interaction(custom_interaction);
							} else {
								tool_turn->add_tool_interaction(std::make_shared<interaction_tool_call>(
								    call.function.name, call.function.name + "(" + arg_preview + ")"));
							}
							if (self->global_queue_) {
								editor_event tool_ev;
								tool_ev.type = event_type::agent_tool_update;
								tool_ev.key_code = self->id_;
								self->global_queue_->push(tool_ev);
							}
						}

						if (!prep.error_message.empty()) {
							tool_result = prep.error_message;
						} else {
							try {
								self->update_last_activity_time();
								tool_result = prep.tool->execute(ctx);
								self->update_last_activity_time();
							} catch (const std::exception &e) {
								self->update_last_activity_time();
								tool_result = "Execution Error: " + std::string(e.what());
							}
						}
					}

					std::string result_preview = tool_result;
					if (result_preview.length() > 1024) {
						result_preview = result_preview.substr(0, 1021) + "...";
					}

					if (!is_silent && !custom_interaction) {
						tool_turn->add_tool_interaction(
						    std::make_shared<interaction_tool_result>(call.function.name, result_preview));
						if (self->global_queue_) {
							editor_event result_ev;
							result_ev.type = event_type::agent_tool_update;
							result_ev.key_code = self->id_;
							self->global_queue_->push(result_ev);
						}
					} else if (!is_silent && custom_interaction) {
						if (self->global_queue_) {
							editor_event result_ev;
							result_ev.type = event_type::agent_tool_update;
							result_ev.key_code = self->id_;
							self->global_queue_->push(result_ev);
						}
					}

					agentlib::tool_result res;
					res.call_id = call.id;
					res.name = call.function.name;
					res.content = tool_result;
					res.is_error = tool_result.starts_with("Error:") || tool_result.starts_with("Verification Error:");
					
					{
						std::lock_guard<std::mutex> lock(self->conversation_mutex_);
						tool_turn->add_result(res);
					}

					// Attempt to zap transient failure loops now that a tool has completed
					auto temp_convo = self->get_conversation();
					self->compact_ephemeral_errors(temp_convo);
				}
				self->current_tool_.clear();
				self->set_status(agent_status::thinking);
			} else {
				auto msgs = response_turn->to_messages(model_capabilities{}, 0);
				final_response = msgs.empty() ? "" : msgs[0].content;

				bool more_user_input = false;
				{
					std::lock_guard<std::mutex> lock(self->conversation_mutex_);
					if (self->conversation_->get_current_episode() && 
						!self->conversation_->get_current_episode()->get_transactions().empty()) {
						auto latest_tx = self->conversation_->get_current_episode()->get_transactions().back();
						if (latest_tx != active_tx) {
							more_user_input = true;
						}
					}
				}
				if (more_user_input) {
					continue; // Loop around to instantly process the queued user prompt!
				}

				self->evaluate_compaction();
				self->evaluate_auto_episode(convo);
				break;
			}
		}

		if (self->is_closed_) {
			event_logger::get_instance().log("Thread exited: ai_agent main loop ({}) [closed at end]", self->id_);
			return;
		}

		if (self->is_exit_implicitly_on_idle()) {
			if (!self->has_final_result()) {
				self->set_final_result(final_response);
			}
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
			for (const auto &interaction : self->get_interactions()) {
				full_history += interaction->get_raw_text() + "\n\n";
			}

			// Mount to global VFS
			skill_manager::get_instance().get_vfs()->mount_buffer(uri, full_history);

			if (self->is_notify_parent_on_completion()) {
				std::string system_msg =
				    "Subagent " + agent_id_str + " (" + self->name_ + ") has finished processing and returned to idle state.\n\n";
				system_msg += "Completion Event Data:\n```json\n" + notification_json.dump(2) + "\n```\n\n";
				system_msg += "You can read the full interaction history log with the fs_read_lines tool from `" + uri + "`";

				parent->inject_context("user", system_msg, true);
			}
		}
	}).detach();
}

std::vector<message> ai_agent::get_conversation_unlocked() const
{
	if (!conversation_) {
		return {};
	}
	model_capabilities caps = model_ ? model_->get_capabilities() : model_capabilities{};
	std::vector<message> messages;
	for (const auto& ep : conversation_->get_episodes()) {
		auto ep_msgs = ep->to_messages(caps);
		messages.insert(messages.end(), ep_msgs.begin(), ep_msgs.end());
	}
	if (auto curr_ep = conversation_->get_current_episode()) {
		auto ep_msgs = curr_ep->to_messages(caps, false);
		for (auto& m : ep_msgs) {
			m.episode_id = "";
			m.episode_level = 0;
		}
		messages.insert(messages.end(), ep_msgs.begin(), ep_msgs.end());
	}
	return messages;
}

std::vector<message> ai_agent::get_conversation() const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	return get_conversation_unlocked();
}

void ai_agent::set_conversation(const std::vector<message> &c)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	set_conversation_unlocked(c);
}

void ai_agent::set_conversation_unlocked(const std::vector<message> &c)
{
	long long next_seq = 1;
	if (conversation_) {
		next_seq = conversation_->get_next_episode_seq();
	}
	conversation_ = std::make_shared<Conversation>();
	conversation_->set_next_episode_seq(next_seq);
	if (model_) {
		conversation_->set_model(model_);
	}
	
	std::map<std::string, std::shared_ptr<Episode>> ep_map;
	std::shared_ptr<Episode> current_ep = nullptr;
	std::shared_ptr<Transaction> current_tx = nullptr;
	
	std::shared_ptr<Episode> last_ep = nullptr;
	for (const auto &msg : c) {
		std::shared_ptr<Episode> ep = nullptr;
		if (!msg.episode_id.empty()) {
			auto it = ep_map.find(msg.episode_id);
			if (it != ep_map.end()) {
				ep = it->second;
			} else {
				ep = std::make_shared<Episode>(msg.episode_id, "Episode " + msg.episode_id, "Reconstructed episode");
				ep->set_compaction_level(msg.episode_level != -1 ? msg.episode_level : 0);
				auto idx_it = episode_index_.find(msg.episode_id);
				if (idx_it != episode_index_.end()) {
					long long seq = idx_it->second.episode_seq;
					ep->set_sequence_number(seq);
					if (seq >= conversation_->get_next_episode_seq()) {
						conversation_->set_next_episode_seq(seq + 1);
					}
				}
				ep_map[msg.episode_id] = ep;
				conversation_->add_episode(ep);
			}
		} else {
			if (!current_ep) {
				std::string active_id = "episode_active";
				current_ep = std::make_shared<Episode>(active_id, "Active Session", "Reconstructed active session");
				conversation_->set_current_episode(current_ep);
			}
			ep = current_ep;
		}

		if (ep != last_ep) {
			current_tx = nullptr;
			last_ep = ep;
		}
		
		transaction_type tx_type = transaction_type::user_exchange;
		if (msg.role == "system") {
			tx_type = transaction_type::system_injection;
		}
		
		bool start_new_tx = !current_tx;
		if (msg.role == "user" || msg.role == "system") {
			start_new_tx = true;
		}
		
		if (start_new_tx) {
			std::string tx_id = "tx_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + std::to_string(std::rand() % 1000);
			current_tx = std::make_shared<Transaction>(tx_id, tx_type);
			ep->add_transaction(current_tx);
		}
		
		std::string turn_id = "turn_" + std::to_string(std::rand());
		std::shared_ptr<Turn> turn = nullptr;
		if (msg.role == "system") {
			turn = std::make_shared<system_turn>(turn_id, msg.content, "reconstructed");
		} else if (msg.role == "user") {
			turn = std::make_shared<user_turn>(turn_id, msg.content, msg.name);
		} else if (msg.role == "assistant") {
			std::vector<tool_call> calls;
			if (msg.tool_calls) {
				calls = *msg.tool_calls;
			}
			turn = std::make_shared<model_response_turn>(turn_id, msg.content, msg.reasoning_content, calls);
		} else if (msg.role == "tool") {
			std::shared_ptr<tool_execution_turn> tool_turn = nullptr;
			if (current_tx && !current_tx->get_turns().empty()) {
				auto last_t = current_tx->get_turns().back();
				if (last_t->get_type() == turn_type::tool_execution) {
					tool_turn = std::dynamic_pointer_cast<tool_execution_turn>(last_t);
				}
			}
			if (!tool_turn) {
				tool_turn = std::make_shared<tool_execution_turn>(turn_id);
				tool_turn->set_sequence_number(conversation_->allocate_next_turn_seq());
				if (current_tx) {
					current_tx->add_turn(tool_turn);
				}
			}
			tool_result res;
			if (msg.tool_call_id) {
				res.call_id = *msg.tool_call_id;
			}
			if (msg.name) {
				res.name = *msg.name;
			}
			res.content = msg.content;
			res.is_error = msg.content.starts_with("Error:");
			tool_turn->add_result(res);
			continue;
		}
		
		if (turn && current_tx) {
			turn->set_sequence_number(conversation_->allocate_next_turn_seq());
			current_tx->add_turn(turn);
		}
	}
}

std::shared_ptr<Conversation> ai_agent::get_conversation_data() const
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	return conversation_;
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
	if (conversation_) {
		root["conversation"] = conversation_->serialize();
	} else {
		root["conversation"] = nlohmann::json::object();
	}

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
	}
}

void ai_agent::snapshot_episode(const std::string &title, const std::string &summary, const std::vector<std::string> &tags)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (!conversation_ || !conversation_->get_current_episode())
		return;

	auto curr_ep = conversation_->get_current_episode();

	int l0_tokens = curr_ep->estimate_token_count(0);
	int l1_tokens = curr_ep->estimate_token_count(1);
	int l2_tokens = curr_ep->estimate_token_count(2);

	std::string episode_id = curr_ep->get_id();
	long long seq = curr_ep->get_sequence_number();
	if (episode_id == "episode_active") {
		seq = conversation_->allocate_next_episode_seq();
		episode_id = "episode_" + std::to_string(seq);
		curr_ep->set_sequence_number(seq);
	}

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";

	curr_ep->set_title(title);
	curr_ep->set_summary(summary);
	curr_ep->set_finalized(true);

	nlohmann::json root = curr_ep->serialize();
	root["tags"] = tags;

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
		event_logger::get_instance().log("Snapshot written to {}", episode_id);
	}

	nlohmann::json meta;
	meta["episode_id"] = episode_id;
	meta["title"] = title;
	meta["summary"] = summary;
	meta["reactivation_hint"] = curr_ep->get_reactivation_hint();
	meta["tags"] = tags;
	meta["episode_seq"] = seq;
	long long l_seq = next_lru_seq_++;
	meta["lru_seq"] = l_seq;
	meta["tokens_level_0"] = l0_tokens;
	meta["tokens_level_1"] = l1_tokens;
	meta["tokens_level_2"] = l2_tokens;

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
	mi.tokens_level_0 = l0_tokens;
	mi.tokens_level_1 = l1_tokens;
	mi.tokens_level_2 = l2_tokens;
	mi.reactivation_hint = curr_ep->get_reactivation_hint();
	episode_index_[episode_id] = mi;
}

void ai_agent::page_out_context(size_t start_index, size_t end_index, const std::string &title, const std::string &summary,
				const std::vector<std::string> &tags)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	last_response_id_.clear();

	auto flat_convo = get_conversation_unlocked();

	// Identify all tool call groups in flat_convo
	std::vector<std::pair<size_t, size_t>> tool_groups;
	for (size_t i = 0; i < flat_convo.size(); ++i) {
		if (flat_convo[i].role == "assistant" && flat_convo[i].tool_calls && !flat_convo[i].tool_calls->empty()) {
			size_t g_start = i;
			size_t g_end = i;
			bool has_pending = false;
			std::set<std::string> ids;
			for (const auto &tc : *flat_convo[i].tool_calls) {
				ids.insert(tc.id);
			}

			for (size_t j = i + 1; j < flat_convo.size(); ++j) {
				if (flat_convo[j].role == "tool" && flat_convo[j].tool_call_id &&
				    ids.count(*flat_convo[j].tool_call_id) > 0) {
					g_end = j;
				}
			}

			for (const auto &tc : *flat_convo[i].tool_calls) {
				bool found = false;
				for (size_t j = i + 1; j < flat_convo.size(); ++j) {
					if (flat_convo[j].role == "tool" && flat_convo[j].tool_call_id &&
					    *flat_convo[j].tool_call_id == tc.id) {
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
				g_end = flat_convo.size();
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

	std::cout << "[page_out_context] start_index=" << start_index << ", end_index=" << end_index << ", flat_convo.size()=" << flat_convo.size() << std::endl;
	if (start_index >= end_index || end_index > flat_convo.size()) {
		std::cout << "[page_out_context] Early return triggered!" << std::endl;
		return;
	}

	long long seq = conversation_->allocate_next_episode_seq();
	std::string episode_id = "episode_" + std::to_string(seq);

	// Construct temp episode to serialize it
	auto temp_ep = std::make_shared<Episode>(episode_id, title, summary);
	temp_ep->set_finalized(true);
	temp_ep->set_sequence_number(seq);
	std::shared_ptr<Transaction> current_tx = nullptr;
	for (size_t i = start_index; i < end_index; ++i) {
		const auto &msg = flat_convo[i];
		transaction_type tx_type = transaction_type::user_exchange;
		if (msg.role == "system") {
			tx_type = transaction_type::system_injection;
		}

		bool start_new_tx = !current_tx;
		if (msg.role == "user" || msg.role == "system") {
			start_new_tx = true;
		}

		if (start_new_tx) {
			std::string tx_id = "tx_" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "_" + std::to_string(std::rand() % 1000);
			current_tx = std::make_shared<Transaction>(tx_id, tx_type);
			temp_ep->add_transaction(current_tx);
		}

		std::string turn_id = "turn_" + std::to_string(std::rand());
		std::shared_ptr<Turn> turn = nullptr;
		if (msg.role == "system") {
			turn = std::make_shared<system_turn>(turn_id, msg.content, "paged_out");
		} else if (msg.role == "user") {
			turn = std::make_shared<user_turn>(turn_id, msg.content, msg.name);
		} else if (msg.role == "assistant") {
			std::vector<tool_call> calls;
			if (msg.tool_calls) {
				calls = *msg.tool_calls;
			}
			turn = std::make_shared<model_response_turn>(turn_id, msg.content, msg.reasoning_content, calls);
		} else if (msg.role == "tool") {
			std::shared_ptr<tool_execution_turn> tool_turn = nullptr;
			if (current_tx && !current_tx->get_turns().empty()) {
				auto last_t = current_tx->get_turns().back();
				if (last_t->get_type() == turn_type::tool_execution) {
					tool_turn = std::dynamic_pointer_cast<tool_execution_turn>(last_t);
				}
			}
			if (!tool_turn) {
				tool_turn = std::make_shared<tool_execution_turn>(turn_id);
				tool_turn->set_sequence_number(conversation_->allocate_next_turn_seq());
				if (current_tx) {
					current_tx->add_turn(tool_turn);
				}
			}
			tool_result res;
			if (msg.tool_call_id) {
				res.call_id = *msg.tool_call_id;
			}
			if (msg.name) {
				res.name = *msg.name;
			}
			res.content = msg.content;
			res.is_error = msg.content.starts_with("Error:");
			tool_turn->add_result(res);
			continue;
		}

		if (turn && current_tx) {
			turn->set_sequence_number(conversation_->allocate_next_turn_seq());
			current_tx->add_turn(turn);
		}
	}

	int l0_tokens = temp_ep->estimate_token_count(0);
	int l1_tokens = temp_ep->estimate_token_count(1);
	int l2_tokens = temp_ep->estimate_token_count(2);

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + episode_id + ".json";
	std::string meta_filepath = history_dir + "/" + episode_id + "_metadata.json";

	nlohmann::json root = temp_ep->serialize();
	root["tags"] = tags;

	std::ofstream file(filepath);
	if (file.is_open()) {
		file << root.dump(4);
		file.close();
	} else {
		event_logger::get_instance().log("Failed to write episode archive to {}", filepath);
		return;
	}

	nlohmann::json meta;
	meta["episode_id"] = episode_id;
	meta["title"] = title;
	meta["summary"] = summary;
	meta["reactivation_hint"] = "";
	meta["tags"] = tags;
	meta["episode_seq"] = seq;
	long long l_seq = next_lru_seq_++;
	meta["lru_seq"] = l_seq;
	meta["tokens_level_0"] = l0_tokens;
	meta["tokens_level_1"] = l1_tokens;
	meta["tokens_level_2"] = l2_tokens;

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
	mi.tokens_level_0 = l0_tokens;
	mi.tokens_level_1 = l1_tokens;
	mi.tokens_level_2 = l2_tokens;
	episode_index_[episode_id] = mi;

	// Replace the block with the summary pointer
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
	summary_msg.episode_level = COMPACTION_LEVEL_PAGED_OUT;

	flat_convo.erase(flat_convo.begin() + start_index, flat_convo.begin() + end_index);
	flat_convo.insert(flat_convo.begin() + start_index, summary_msg);

	// Rebuild the hierarchy
	set_conversation_unlocked(flat_convo);

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

				if (conversation_ && mi.episode_seq >= conversation_->get_next_episode_seq()) {
					conversation_->set_next_episode_seq(mi.episode_seq + 1);
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

	auto flat_convo = get_conversation_unlocked();
	std::cout << "[page_out_prior_context] called. target_episode_id=" << target_episode_id 
	          << ", include_all_prior=" << include_all_prior 
	          << ", flat_convo.size()=" << flat_convo.size() << std::endl;
	if (flat_convo.size() < 3) {
		std::cout << "[page_out_prior_context] flat_convo.size() < 3, early return!" << std::endl;
		return; // Nothing to compress
	}

	size_t end_index = flat_convo.size() - 2; // Default to current

	// 1. Find the upper boundary
	if (!target_episode_id.empty()) {
		bool found = false;
		// Search backwards for the specific episode marker
		for (int i = static_cast<int>(flat_convo.size()) - 2; i >= 0; --i) {
			if (flat_convo[i].role == "system" && flat_convo[i].content.find(target_episode_id) != std::string::npos) {
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
		for (int i = static_cast<int>(flat_convo.size()) - 2; i >= 0; --i) {
			if (flat_convo[i].role == "tool" && flat_convo[i].name == "agent_mark_episode") {
				end_index = i + 1;
				break;
			}
			if (i > 0 && flat_convo[i].role == "system" && flat_convo[i].content.find("Episode Archived") != std::string::npos) {
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
			if (flat_convo[i].role == "system" || flat_convo[i].role == "user") {
				start_index = i + 1;
				break;
			}
		}
	}

	std::cout << "[page_out_prior_context] start_index=" << start_index << ", end_index=" << end_index << std::endl;
	if (start_index >= end_index) {
		std::cout << "[page_out_prior_context] start_index >= end_index early return!" << std::endl;
		event_logger::get_instance().log("Context too small to page out naturally.");
		return;
	}

	// Unlock and delegate to the core paging function
	lock.unlock();
	std::cout << "[page_out_prior_context] Delegating to page_out_context..." << std::endl;
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
		last_response_id_.clear();
		
		auto flat = get_conversation_unlocked();
		bool flat_compacted = false;
		while (flat.size() >= 4) {
			auto it_n0 = flat.end() - 1;
			auto it_n1 = flat.end() - 2;
			auto it_n2 = flat.end() - 3;
			auto it_n3 = flat.end() - 4;

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
			flat.erase(it_n3, it_n1);
			flat_compacted = true;
		}
		if (flat_compacted) {
			set_conversation_unlocked(flat);
		}
		event_logger::get_instance().log("Agent {} zapped ephemeral errors from context.", name_);
	}
}

void ai_agent::replace_tool_result(const std::string &tool_call_id, const std::string &new_content)
{
	std::lock_guard<std::mutex> lock(conversation_mutex_);
	if (!conversation_)
		return;

	auto search_in_episode = [&](const std::shared_ptr<Episode>& ep) -> bool {
		if (!ep) return false;
		for (const auto& tx : ep->get_transactions()) {
			for (const auto& turn : tx->get_turns()) {
				if (turn->get_type() == turn_type::tool_execution) {
					auto tool_turn = std::dynamic_pointer_cast<tool_execution_turn>(turn);
					if (tool_turn) {
						for (const auto& res : tool_turn->get_results()) {
							if (res.call_id == tool_call_id) {
								tool_turn->update_result_content(tool_call_id, new_content);
								return true;
							}
						}
					}
				}
			}
		}
		return false;
	};

	bool found = false;
	if (auto curr_ep = conversation_->get_current_episode()) {
		found = search_in_episode(curr_ep);
	}
	if (!found) {
		for (const auto& ep : conversation_->get_episodes()) {
			if (search_in_episode(ep)) {
				found = true;
				break;
			}
		}
	}

	if (found) {
		last_response_id_.clear();
		if (global_queue_) {
			editor_event ev;
			ev.type = event_type::agent_tool_update;
			ev.key_code = id_;
			global_queue_->push(ev);
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
		if (i > 0 && convo[i].role == "system" && convo[i].content.find("Episode Archived") != std::string::npos)
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

			const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
			bool skip_weights = (in_testsuite && std::string(in_testsuite) == "1");
			if (!skip_weights && !turbostar::context_dnn::get_instance().is_loaded()) {
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
			std::string content = "[SYSTEM MEMORY: Episode Archived]\nTitle: Auto-Episode\nSummary: " + reason_msg + "\nTags: [auto-episode]";
			
			std::string tx_id = "tx_auto_split_" + std::to_string(std::rand());
			auto tx = std::make_shared<Transaction>(tx_id, transaction_type::system_injection);
			std::string turn_id = "turn_auto_split_" + std::to_string(std::rand());
			auto turn = std::make_shared<system_turn>(turn_id, content, "episode_archival");
			turn->set_sequence_number(conversation_->allocate_next_turn_seq());
			tx->add_turn(turn);
			
			conversation_->add_transaction(tx);

			message marker_msg;
			marker_msg.role = "system";
			marker_msg.content = content;
			convo.push_back(marker_msg);
		}

		snapshot_episode("Auto-Episode", reason_msg, {"auto-episode"});

		{
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			conversation_->archive_current_episode("Auto-Episode", reason_msg);
			conversation_->create_new_episode("episode_" + std::to_string(conversation_->allocate_next_episode_seq()), "Active Session", "");
		}
		add_interaction(std::make_shared<interaction_system_message>("Auto-Episode boundary inserted (" + reason_msg + ")."));
	}
}

void ai_agent::evaluate_compaction()
{
	if (!model_)
		return;

	if (!is_mutation_possible()) {
		// Calculate target bounds based on model's max_context_tokens
		int max_tokens = model_->get_max_context_tokens();
		double upper_pct = 0.8;
		int upper_bound = static_cast<int>(max_tokens * upper_pct);

		int current_active_tokens = calculate_current_tokens();
		active_tokens_.store(current_active_tokens);

		if (current_active_tokens > upper_bound) {
			std::string prev_id;
			{
				std::lock_guard<std::mutex> lock(conversation_mutex_);
				prev_id = last_response_id_;
			}
			if (!prev_id.empty()) {
				event_logger::get_instance().log(
				    std::format("Active history tokens ({}) exceed upper bound ({}) in stateful mode. "
						"Triggering Responses API compaction on server.",
						current_active_tokens, upper_bound));
				std::string error_msg;
				std::string compacted_id = client_->compact_response(prev_id, &error_msg);
				if (!compacted_id.empty()) {
					std::lock_guard<std::mutex> lock(conversation_mutex_);
					last_response_id_ = compacted_id;

					if (conversation_) {
						conversation_->archive_current_episode("Compacted History", "Compacted stateful session");
						conversation_->create_new_episode("episode_" + std::to_string(conversation_->allocate_next_episode_seq()), "Active Episode", "");
					}
					event_logger::get_instance().log(
					    std::format("Responses API compaction succeeded. New response ID: {}", compacted_id));
				} else {
					event_logger::get_instance().log(
					    std::format("Responses API automatic compaction failed: {}", error_msg));
				}
			}
		}
		return;
	}

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
		if (conversation_) {
			for (const auto &ep : conversation_->get_episodes()) {
				int level = ep->get_compaction_level();
				if (level != COMPACTION_LEVEL_PAGED_OUT) {
					active_episodes[ep->get_id()] = level;
					auto it = episode_index_.find(ep->get_id());
					if (it != episode_index_.end()) {
						if (level == 0) {
							current_active_tokens += it->second.tokens_level_0;
						} else if (level == 1) {
							current_active_tokens += it->second.tokens_level_1;
						} else if (level == 2) {
							current_active_tokens += it->second.tokens_level_2;
						}
					} else {
						current_active_tokens += ep->estimate_token_count(level);
					}
				}
			}

			if (auto curr_ep = conversation_->get_current_episode()) {
				int level = curr_ep->get_compaction_level();
				if (level != COMPACTION_LEVEL_PAGED_OUT) {
					active_episodes[curr_ep->get_id()] = level;
					auto it = episode_index_.find(curr_ep->get_id());
					if (it != episode_index_.end()) {
						if (level == 0) {
							current_active_tokens += it->second.tokens_level_0;
						} else if (level == 1) {
							current_active_tokens += it->second.tokens_level_1;
						} else if (level == 2) {
							current_active_tokens += it->second.tokens_level_2;
						}
					} else {
						current_active_tokens += curr_ep->estimate_token_count(level);
					}
				}
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

void ai_agent::force_compaction()
{
	if (!model_)
		return;

	if (!is_mutation_possible()) {
		std::string prev_id;
		{
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			prev_id = last_response_id_;
		}
		if (!prev_id.empty()) {
			event_logger::get_instance().log("Forcing Responses API compaction on server.");
			std::string error_msg;
			std::string compacted_id = client_->compact_response(prev_id, &error_msg);
			if (!compacted_id.empty()) {
				std::lock_guard<std::mutex> lock(conversation_mutex_);
				last_response_id_ = compacted_id;

				if (conversation_) {
					conversation_->archive_current_episode("Compacted History", "Compacted stateful session");
					conversation_->create_new_episode("episode_" + std::to_string(conversation_->allocate_next_episode_seq()), "Active Episode", "");
				}
				add_interaction(std::make_shared<interaction_system_message>(
				    std::format("Responses API compaction succeeded. New response ID: {}", compacted_id)));
			} else {
				add_interaction(std::make_shared<interaction_system_message>(
				    std::format("Responses API compaction failed: {}", error_msg)));
			}
		} else {
			add_interaction(std::make_shared<interaction_system_message>("No active response session to compact."));
		}
	} else {
		add_interaction(std::make_shared<interaction_system_message>(
		    "Manual compaction (/compact) is only supported/needed in stateful response mode. "
		    "For stateless models, use /pageout to archive/compress history."));
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

		if (conversation_) {
			for (const auto &ep : conversation_->get_episodes()) {
				if (ep->get_id() == episode_id) {
					ep->set_reactivation_hint(hint);
					break;
				}
			}
			if (auto curr_ep = conversation_->get_current_episode()) {
				if (curr_ep->get_id() == episode_id) {
					curr_ep->set_reactivation_hint(hint);
				}
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

			bool has_data = false;
			std::string context_dump;
			if (root.contains("transactions") && root["transactions"].is_array()) {
				context_dump = root["transactions"].dump(2);
				has_data = true;
			} else if (root.contains("conversation") && root["conversation"].is_array()) {
				context_dump = root["conversation"].dump(2);
				has_data = true;
			}

			if (has_data) {
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

				std::string summary_model_id = config_manager::get_instance().get_task_model_id("episode_summarizer");
				auto default_model = ai_model_registry::get_instance().get_model(summary_model_id);
				if (!default_model) {
					default_model = ai_model_registry::get_instance().get_default_model();
				}
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
				if (default_model->get_api_type() == api_type::copilot) {
					transport->set_token_provider([]() { return copilot_manager::get_instance().get_copilot_token(); });
				}
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
	if (!conversation_) {
		return segments;
	}

	for (const auto &ep : conversation_->get_episodes()) {
		compaction_segment seg;
		seg.label = ep->get_title();
		if (seg.label.empty()) {
			seg.label = ep->get_id();
		}
		seg.current_level = ep->get_compaction_level();

		auto it = episode_index_.find(ep->get_id());
		if (it != episode_index_.end()) {
			seg.uncompacted_tokens = it->second.tokens_level_0;
		} else {
			seg.uncompacted_tokens = ep->estimate_token_count(0);
		}
		segments.push_back(seg);
	}

	if (auto curr_ep = conversation_->get_current_episode()) {
		compaction_segment seg;
		seg.label = "Recent";
		seg.current_level = curr_ep->get_compaction_level();
		seg.uncompacted_tokens = curr_ep->estimate_token_count(0);
		segments.push_back(seg);
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

	if (conversation_) {
		auto curr_ep = conversation_->get_current_episode();
		if (!curr_ep) {
			curr_ep = conversation_->create_new_episode("episode_" + std::to_string(conversation_->allocate_next_episode_seq()), "Active Session", "");
		}

		std::string tx_id = "tx_archived_summary_" + std::to_string(std::rand());
		auto tx = std::make_shared<Transaction>(tx_id, transaction_type::system_injection);
		std::string turn_id = "turn_archived_summary_" + std::to_string(std::rand());
		auto turn = std::make_shared<system_turn>(turn_id, oss.str(), "reactivation_hint");
		turn->set_sequence_number(conversation_->allocate_next_turn_seq());
		tx->add_turn(turn);

		if (!curr_ep->get_transactions().empty()) {
			curr_ep->insert_transaction(1, tx);
		} else {
			curr_ep->add_transaction(tx);
		}
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

void ai_agent::set_exit_implicitly_on_idle(bool val)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	exit_implicitly_on_idle_ = val;
}

bool ai_agent::is_exit_implicitly_on_idle() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return exit_implicitly_on_idle_;
}

void ai_agent::set_notify_parent_on_completion(bool val)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	notify_parent_on_completion_ = val;
}

bool ai_agent::is_notify_parent_on_completion() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return notify_parent_on_completion_;
}

std::string ai_agent::get_animation_name() const
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	return animation_name_;
}

void ai_agent::set_animation_name(const std::string &name)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	animation_name_ = name;
}


std::string ai_agent::get_allowed_write_file() const
{
	std::lock_guard<std::mutex> lock(const_cast<std::mutex &>(state_mutex_));
	return allowed_write_file_;
}

void ai_agent::set_allowed_write_file(const std::string &path)
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	allowed_write_file_ = path;
}

bool ai_agent::is_read_only() const
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	return properties_.read_only;
}

void ai_agent::set_read_only(bool ro)
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	properties_.read_only = ro;
}

agent_role ai_agent::get_role() const
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	return properties_.role;
}

void ai_agent::set_role(agent_role r)
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	properties_.role = r;
	properties_.read_only = (r == agent_role::summarizer);
	if (r == agent_role::summarizer) {
		properties_.active_families.clear();
	} else {
		if (std::find(properties_.active_families.begin(), properties_.active_families.end(), "base") == properties_.active_families.end()) {
			properties_.active_families.insert(properties_.active_families.begin(), "base");
		}
	}
}

agent_properties ai_agent::get_properties() const
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	return properties_;
}

void ai_agent::set_properties(const agent_properties &props)
{
	std::lock_guard<std::mutex> lock(properties_mutex_);
	properties_ = props;
}

void ai_agent::set_planning(bool planning, size_t start_index)
{
	is_planning_.store(planning);
	if (planning) {
		planning_start_index_ = start_index;
		set_read_only(true);
	} else {
		set_plan_file("");
		set_read_only(get_role() == agent_role::summarizer);
	}
}

} // namespace agentlib
