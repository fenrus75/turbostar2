#include "ai_agent.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../config_manager.h"
#include "../event_queue.h"
#include "../event_logger.h"
#include "../git_manager.h"
#include "../fs_utils.h"
#include "httplib_transport.h"
#include "skill_manager.h"

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

bool ai_agent::page_in_context(const std::string& milestone_id, int compression_level)
{
	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/" + milestone_id + ".json";
	if (!std::filesystem::exists(filepath)) return false;

	try {
		std::ifstream file(filepath);
		nlohmann::json root;
		file >> root;

		if (root.contains("conversation") && root["conversation"].is_array()) {
			std::lock_guard<std::mutex> lock(conversation_mutex_);

			// Find the pointer message to replace it
			auto it = std::find_if(conversation_.begin(), conversation_.end(), [&](const message& msg) {
				return msg.role == "system" && msg.content.find("Raw history archive: " + milestone_id) != std::string::npos;
			});

			std::vector<message> loaded_msgs;
			for (const auto& item : root["conversation"]) {
				message msg;
				from_json(item, msg);
				
				// Level 1: Strip explicit reasoning content natively provided by models like DeepSeek.
				if (compression_level >= 1 && msg.role == "assistant") {
					if (msg.reasoning_content) {
						msg.reasoning_content.reset();
						increment_stat("explicit_think_blocks_stripped");
					}
				}
				
				// Level 2: Strip conversational pseudo-reasoning (used by GPT-4o/Gemini) if a tool call was made.
				if (compression_level >= 2 && msg.role == "assistant") {
					if (msg.tool_calls && !msg.tool_calls->empty() && !msg.content.empty()) {
						msg.content.clear();
						increment_stat("pseudo_think_blocks_stripped");
					}
				}
				
				loaded_msgs.push_back(msg);
			}

			if (it != conversation_.end()) {
				// Replace the pointer with the actual messages
				it = conversation_.erase(it);
				conversation_.insert(it, loaded_msgs.begin(), loaded_msgs.end());
			} else {
				// If pointer not found, just append to the end
				conversation_.insert(conversation_.end(), loaded_msgs.begin(), loaded_msgs.end());
			}

			// Update access time
			auto now = std::chrono::system_clock::now();
			long long epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
			const char* mock_epoch_env = std::getenv("TURBOSTAR_TEST_MOCK_EPOCH");
			if (mock_epoch_env) {
				try { epoch = std::stoll(mock_epoch_env); } catch (...) {}
			}

			if (milestone_index_.find(milestone_id) != milestone_index_.end()) {
				milestone_index_[milestone_id].last_accessed_epoch = epoch;
			}
			
			// Update the metadata file
			std::string meta_filepath = history_dir + "/" + milestone_id + "_metadata.json";
			try {
				std::ifstream mfile(meta_filepath);
				nlohmann::json meta_root;
				mfile >> meta_root;
				meta_root["last_accessed_epoch"] = epoch;
				std::ofstream mfile_out(meta_filepath);
				mfile_out << meta_root.dump(4);
			} catch (...) {}

			event_logger::get_instance().log("Agent " + name_ + " paged IN context " + milestone_id);
			increment_stat("context_pages_in");
			return true;
		}
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to page in context: " + std::string(e.what()));
	}
	return false;
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

	load_milestone_index();

	std::string history_dir = fs_utils::get_project_history_dir(name_);
	std::string filepath = history_dir + "/active_state.json";
	if (!std::filesystem::exists(filepath)) return false;

	try {
		std::ifstream file(filepath);
		nlohmann::json root;
		file >> root;

		if (root.contains("conversation") && root["conversation"].is_array()) {
			std::lock_guard<std::mutex> lock(conversation_mutex_);
			conversation_.clear();
			for (const auto& item : root["conversation"]) {
				message msg;
				from_json(item, msg);
				conversation_.push_back(msg);
			}
			event_logger::get_instance().log("Agent " + name_ + " restored active state from " + filepath);
			return true;
		}
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to restore active state: " + std::string(e.what()));
	}
	return false;
}

void ai_agent::close()
{
	if (!is_closed_) {
		// Implicit Milestone: Page out all uncompressed history when the editor closes
		// so the agent boots up "fresh" (but with pointers) next session.
		page_out_prior_context("", true, "End of Session", "The user closed the editor or agent window. This session was automatically paged out.", {"session-end"});
		
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
	status_cv_.wait(lock, [this]() {
		return status_ == agent_status::idle || status_ == agent_status::error;
	});
}

void ai_agent::cancel_current_task()
{
	if (client_) {
		client_->cancel();
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

void ai_agent::increment_stat(const std::string& key, int amount)
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
bool ai_agent::mark_todo_complete(const std::string &text_match, std::string &out_error){
	std::lock_guard<std::mutex> lock(state_mutex_);
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

	add_interaction(std::make_shared<interaction_system_message>("Model switched to: " + model_->get_name() + " (" +
								       model_->get_id() + ")"));
}

void ai_agent::submit_prompt(const std::string &prompt_text)
{
	{
		std::lock_guard<std::mutex> lock(conversation_mutex_);
		message user_msg;
		user_msg.role = "user";
		user_msg.content = prompt_text;
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
		event_logger::get_instance().log("Thread started: ai_agent main loop (" + std::to_string(self->id_) + ")");
		size_t last_synced_index = 0;
		std::vector<message> convo;

		auto &registry = tool_registry::get_instance();
		tool_context ctx;

		std::string git_root = git_manager::get_instance().get_repository_root();
		std::filesystem::path workspace_root;
		if (!git_root.empty()) {
			workspace_root = std::filesystem::path(git_root);
		} else {
			workspace_root = std::filesystem::current_path();
		}

		ctx.fs_security.set_working_directory(workspace_root);
		ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
		ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
		ctx.fs_security.set_vfs(skill_manager::get_instance().get_vfs());
		ctx.doc_provider = self->doc_provider_;
		ctx.queue = self->global_queue_;
		ctx.active_agent = self.get();

		std::string final_response;

		while (true) {
			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop (" + std::to_string(self->id_) + ") [closed early]");
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

			self->set_status(agent_status::thinking);

			std::shared_ptr<interaction_reasoning> current_reasoning = nullptr;
			std::shared_ptr<interaction_llm_response> current_response = nullptr;
			std::vector<tool_call> accumulated_tool_calls;
			message response_msg;
			response_msg.role = "assistant";

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
			    &registry);

			if (self->is_closed_) {
				event_logger::get_instance().log("Thread exited: ai_agent main loop (" + std::to_string(self->id_) + ") [closed early]");
				return;
			}

			if (!accumulated_tool_calls.empty()) {
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
				for (const auto &call : accumulated_tool_calls) {
					if (self->is_closed_) {
						event_logger::get_instance().log("Thread exited: ai_agent main loop (" + std::to_string(self->id_) + ") [closed in callback]");
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

					ctx.tool_call_id = call.id;
					auto prep = registry.prepare_tool(call.function.name, call.function.arguments, ctx);
					std::string tool_result;
					std::shared_ptr<agent_interaction> custom_interaction;

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

					if (!prep.error_message.empty()) {
						tool_result = prep.error_message;
					} else {
						try {
							tool_result = prep.tool->execute(ctx);
						} catch (const std::exception &e) {
							tool_result = "Execution Error: " + std::string(e.what());
						}
					}

					std::string result_preview = tool_result;
					if (result_preview.length() > 1024) {
						result_preview = result_preview.substr(0, 1021) + "...";
					}

					if (!is_silent && !custom_interaction) {
						self->add_interaction(std::make_shared<interaction_tool_result>(call.function.name, result_preview));
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
				
				break;
			}
		}

		if (self->is_closed_) {
			event_logger::get_instance().log("Thread exited: ai_agent main loop (" + std::to_string(self->id_) + ") [closed at end]");
			return;
		}

		self->set_status(agent_status::idle);
		event_logger::get_instance().log("Agent " + std::to_string(self->id_) + " went idle. Cumulative tokens: Tx=" + std::to_string(self->tokens_tx_) + " Rx=" + std::to_string(self->tokens_rx_) + " Cached=" + std::to_string(self->tokens_cached_));

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

void ai_agent::save_conversation(const std::string& filepath) const
{
        std::lock_guard<std::mutex> lock(conversation_mutex_);
        nlohmann::json root;
        root["agent_id"] = id_;
        root["agent_name"] = name_;
        nlohmann::json conv_array = nlohmann::json::array();
        for (const auto& msg : conversation_) {
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

void ai_agent::snapshot_milestone(const std::string& title, const std::string& summary, const std::vector<std::string>& tags)
{
    std::lock_guard<std::mutex> lock(conversation_mutex_);

    if (conversation_.empty()) return;

    nlohmann::json block_array = nlohmann::json::array();
    int l0_chars = 0;
    int l1_chars = 0;
    int l2_chars = 0;
    
    for (const auto& msg : conversation_) {
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

    auto now = std::chrono::system_clock::now();
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    const char* mock_epoch_env = std::getenv("TURBOSTAR_TEST_MOCK_EPOCH");
    if (mock_epoch_env) {
        try {
            epoch = std::stoll(mock_epoch_env);
        } catch (...) {}
    }
    
    std::string milestone_id = "milestone_" + std::to_string(epoch);

    std::string history_dir = fs_utils::get_project_history_dir(name_);
    std::string filepath = history_dir + "/" + milestone_id + ".json";
    std::string meta_filepath = history_dir + "/" + milestone_id + "_metadata.json";

    nlohmann::json root;
    root["milestone_id"] = milestone_id;
    root["title"] = title;
    root["summary"] = summary;
    root["tags"] = tags;
    root["conversation"] = block_array;

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << root.dump(4);
        event_logger::get_instance().log("Snapshot written to " + milestone_id);
    }
    
    nlohmann::json meta;
    meta["milestone_id"] = milestone_id;
    meta["title"] = title;
    meta["summary"] = summary;
    meta["reactivation_hint"] = ""; // Filled asynchronously
    meta["tags"] = tags;
    meta["created_at_epoch"] = epoch;
    meta["last_accessed_epoch"] = epoch;
    meta["tokens_level_0"] = l0_chars / 4;
    meta["tokens_level_1"] = l1_chars / 4;
    meta["tokens_level_2"] = l2_chars / 4;
    
    std::ofstream meta_file(meta_filepath);
    if (meta_file.is_open()) {
        meta_file << meta.dump(4);
    }
    
    milestone_index_entry mi;
    mi.id = milestone_id;
    mi.title = title;
    mi.summary = summary;
    mi.tags = tags;
    mi.created_at_epoch = epoch;
    mi.last_accessed_epoch = epoch;
    mi.tokens_level_0 = l0_chars / 4;
    mi.tokens_level_1 = l1_chars / 4;
    mi.tokens_level_2 = l2_chars / 4;
    milestone_index_[milestone_id] = mi;
}

void ai_agent::page_out_context(size_t start_index, size_t end_index, const std::string& title, const std::string& summary, const std::vector<std::string>& tags)
{
    std::lock_guard<std::mutex> lock(conversation_mutex_);

    if (start_index >= end_index || end_index > conversation_.size()) return;

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

    // Generate an ID for this milestone (timestamp or random hash)
    auto now = std::chrono::system_clock::now();
    long long epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    
    const char* mock_epoch_env = std::getenv("TURBOSTAR_TEST_MOCK_EPOCH");
    if (mock_epoch_env) {
        try {
            epoch = std::stoll(mock_epoch_env);
        } catch (...) {}
    }
    
    std::string milestone_id = "milestone_" + std::to_string(epoch);

    std::string history_dir = fs_utils::get_project_history_dir(name_);
    std::string filepath = history_dir + "/" + milestone_id + ".json";
    std::string meta_filepath = history_dir + "/" + milestone_id + "_metadata.json";

    nlohmann::json root;
    root["milestone_id"] = milestone_id;
    root["title"] = title;
    root["summary"] = summary;
    root["tags"] = tags;
    root["conversation"] = block_array;

    std::ofstream file(filepath);
    if (file.is_open()) {
        file << root.dump(4);
        file.close();
    } else {
        event_logger::get_instance().log("Failed to write milestone archive to " + filepath);
        return; // Don't delete history if we couldn't save it
    }

    nlohmann::json meta;
    meta["milestone_id"] = milestone_id;
    meta["title"] = title;
    meta["summary"] = summary;
    meta["reactivation_hint"] = ""; // Filled asynchronously
    meta["tags"] = tags;
    meta["created_at_epoch"] = epoch;
    meta["last_accessed_epoch"] = epoch;
    meta["tokens_level_0"] = l0_chars / 4;
    meta["tokens_level_1"] = l1_chars / 4;
    meta["tokens_level_2"] = l2_chars / 4;
    
    std::ofstream meta_file(meta_filepath);
    if (meta_file.is_open()) {
        meta_file << meta.dump(4);
    }
    
    milestone_index_entry mi;
    mi.id = milestone_id;
    mi.title = title;
    mi.summary = summary;
    mi.tags = tags;
    mi.created_at_epoch = epoch;
    mi.last_accessed_epoch = epoch;
    mi.tokens_level_0 = l0_chars / 4;
    mi.tokens_level_1 = l1_chars / 4;
    mi.tokens_level_2 = l2_chars / 4;
    milestone_index_[milestone_id] = mi;

    // 2. Replace the block with the summary pointer
    std::stringstream pointer_msg;
    pointer_msg << "[SYSTEM MEMORY: Milestone Reached]\n";
    pointer_msg << "Title: " << title << "\n";
    pointer_msg << "Summary: " << summary << "\n";
    if (!tags.empty()) {
        pointer_msg << "Tags: [";
        for (size_t i = 0; i < tags.size(); ++i) {
            pointer_msg << tags[i] << (i < tags.size() - 1 ? ", " : "");
        }
        pointer_msg << "]\n";
    }
    pointer_msg << "Raw history archive: " << milestone_id;

    message summary_msg;
    summary_msg.role = "system";
    summary_msg.content = pointer_msg.str();

    conversation_.erase(conversation_.begin() + start_index, conversation_.begin() + end_index);
    conversation_.insert(conversation_.begin() + start_index, summary_msg);

    event_logger::get_instance().log("Paged out " + std::to_string(end_index - start_index) + " turns to " + milestone_id);
    increment_stat("context_pages_out");

    {
        std::lock_guard<std::mutex> lock(summary_mutex_);
        summary_queue_.push_back({milestone_id, filepath});
    }
    summary_cv_.notify_one();
}

void ai_agent::load_milestone_index()
{
    std::lock_guard<std::mutex> lock(conversation_mutex_);
    milestone_index_.clear();
    
    std::string history_dir = fs_utils::get_project_history_dir(name_);
    if (!std::filesystem::exists(history_dir)) return;

    for (const auto& entry : std::filesystem::directory_iterator(history_dir)) {
        std::string filename = entry.path().filename().string();
        if (entry.is_regular_file() && filename.ends_with("_metadata.json")) {
            try {
                std::ifstream f(entry.path());
                nlohmann::json root;
                f >> root;

                milestone_index_entry mi;
                mi.id = root.value("milestone_id", "unknown");
                mi.title = root.value("title", "Untitled");
                mi.summary = root.value("summary", "");
                mi.reactivation_hint = root.value("reactivation_hint", "");
                mi.created_at_epoch = root.value("created_at_epoch", 0LL);
                mi.last_accessed_epoch = root.value("last_accessed_epoch", mi.created_at_epoch);
                mi.tokens_level_0 = root.value("tokens_level_0", 0);
                mi.tokens_level_1 = root.value("tokens_level_1", 0);
                mi.tokens_level_2 = root.value("tokens_level_2", 0);
                
                if (root.contains("tags") && root["tags"].is_array()) {
                    for (const auto& tag : root["tags"]) {
                        mi.tags.push_back(tag.get<std::string>());
                    }
                }
                
                milestone_index_[mi.id] = mi;

                if (mi.reactivation_hint.empty()) {
                    std::string episode_filepath = history_dir + "/" + mi.id + ".json";
                    std::lock_guard<std::mutex> slock(summary_mutex_);
                    summary_queue_.push_back({mi.id, episode_filepath});
                    summary_cv_.notify_one();
                }
            } catch (...) {}
        }
    }
}

std::string ai_agent::get_memory_index() const
{
    std::lock_guard<std::mutex> lock(conversation_mutex_);
    if (milestone_index_.empty()) {
        return "Memory index is empty (no saved milestones).";
    }

    std::stringstream out;
    out << "Agent Memory Index (Paged-Out Milestones):\n";

    // Sort milestones by creation date
    std::vector<const milestone_index_entry*> sorted;
    for (const auto& pair : milestone_index_) {
        sorted.push_back(&pair.second);
    }
    std::sort(sorted.begin(), sorted.end(), [](const milestone_index_entry* a, const milestone_index_entry* b) {
        return a->created_at_epoch < b->created_at_epoch;
    });

    for (const auto* mi : sorted) {
        out << "- [" << mi->id << "] " << mi->title << " (~" << mi->tokens_level_1 << " tokens paged-out)\n";
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

void ai_agent::page_out_prior_context(const std::string& target_milestone_id, bool include_all_prior, const std::string& title, const std::string& summary, const std::vector<std::string>& tags)
{
    std::unique_lock<std::mutex> lock(conversation_mutex_);
    
    if (conversation_.size() < 3) return; // Nothing to compress
    
    size_t end_index = conversation_.size() - 2; // Default to current
    
    // 1. Find the upper boundary
    if (!target_milestone_id.empty()) {
        bool found = false;
        // Search backwards for the specific milestone marker
        for (int i = static_cast<int>(conversation_.size()) - 2; i >= 0; --i) {
            if (conversation_[i].role == "system" && conversation_[i].content.find(target_milestone_id) != std::string::npos) {
                end_index = i; // The boundary is exactly at the target milestone
                found = true;
                break;
            }
        }
        if (!found) {
            event_logger::get_instance().log("Failed to find target milestone: " + target_milestone_id);
            return;
        }
    } else {
        // If no target provided, scan backwards to find the most recent milestone marker
        // We look for either the tool result from agent_mark_milestone OR a previously injected pointer.
        for (int i = static_cast<int>(conversation_.size()) - 2; i >= 0; --i) {
            if (conversation_[i].role == "tool" && conversation_[i].name == "agent_mark_milestone") {
                end_index = i;
                break;
            }
            if (conversation_[i].role == "system" && conversation_[i].content.find("Milestone Reached") != std::string::npos) {
                end_index = i;
                break;
            }
        }
    }
    
    size_t start_index = 1; // Default to after the root system prompt
    
    // 2. Find the lower boundary
    if (!include_all_prior && end_index > 0) {
        // Scan backward from end_index to find the previous milestone/system marker
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

void ai_agent::compact_ephemeral_errors(std::vector<message>& convo){
	bool compacted = false;
	
	while (convo.size() >= 4) {
		auto it_n0 = convo.end() - 1;
		auto it_n1 = convo.end() - 2;
		auto it_n2 = convo.end() - 3;
		auto it_n3 = convo.end() - 4;

		if (it_n0->role != "tool") break;
		if (it_n1->role != "assistant" || !it_n1->tool_calls || it_n1->tool_calls->size() != 1) break;
		if (it_n2->role != "tool") break;
		if (it_n3->role != "assistant" || !it_n3->tool_calls || it_n3->tool_calls->size() != 1) break;

		if (!it_n0->name || !it_n2->name) break;
		if (*it_n0->name != *it_n2->name) break;

		std::string tool_name = *it_n0->name;
		if (it_n1->tool_calls->at(0).function.name != tool_name || it_n3->tool_calls->at(0).function.name != tool_name) break;

		auto is_error = [](const std::string& content) {
			return content.starts_with("Error:") || 
			       content.starts_with("Verification Error:") ||
			       content.starts_with("Stage 1 Security Violation:") ||
			       content.starts_with("Stage 2 Security Violation:");
		};

		if (is_error(it_n0->content)) break; // N-0 must be a success
		if (!is_error(it_n2->content)) break; // N-2 must be a failure

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

			if (it_n0->role != "tool") break;
			if (it_n1->role != "assistant" || !it_n1->tool_calls || it_n1->tool_calls->size() != 1) break;
			if (it_n2->role != "tool") break;
			if (it_n3->role != "assistant" || !it_n3->tool_calls || it_n3->tool_calls->size() != 1) break;

			if (!it_n0->name || !it_n2->name) break;
			if (*it_n0->name != *it_n2->name) break;

			std::string tool_name = *it_n0->name;
			if (it_n1->tool_calls->at(0).function.name != tool_name || it_n3->tool_calls->at(0).function.name != tool_name) break;

			auto is_error = [](const std::string& content) {
				return content.starts_with("Error:") || 
				       content.starts_with("Verification Error:") ||
				       content.starts_with("Stage 1 Security Violation:") ||
				       content.starts_with("Stage 2 Security Violation:");
			};

			if (is_error(it_n0->content)) break;
			if (!is_error(it_n2->content)) break;

			it_n1->content.clear();
			conversation_.erase(it_n3, it_n1);
		}
		event_logger::get_instance().log("Agent " + name_ + " zapped ephemeral errors from context.");
	}
}

void ai_agent::replace_tool_result(const std::string& tool_call_id, const std::string& new_content)
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


void ai_agent::update_milestone_hint(const std::string& milestone_id, const std::string& hint)
{
    // Update the index memory
    {
        std::lock_guard<std::mutex> lock(conversation_mutex_);
        if (milestone_index_.find(milestone_id) != milestone_index_.end()) {
            milestone_index_[milestone_id].reactivation_hint = hint;
        }

        // Mutate the active conversation
        for (auto& msg : conversation_) {
            if (msg.role == "system" && msg.content.find(milestone_id) != std::string::npos) {
                // We found the marker, append the hint
                msg.content += "\nDemand-Load Hint: " + hint;
                break;
            }
        }
    }
    
    // Rewrite the metadata sidecar
    std::string history_dir = fs_utils::get_project_history_dir(name_);
    std::string meta_filepath = history_dir + "/" + milestone_id + "_metadata.json";
    if (std::filesystem::exists(meta_filepath)) {
        try {
            std::ifstream file(meta_filepath);
            nlohmann::json root;
            file >> root;
            root["reactivation_hint"] = hint;
            std::ofstream out(meta_filepath);
            out << root.dump(4);
        } catch (...) {}
    }
}

void ai_agent::summary_worker_loop()
{
    event_logger::get_instance().log("Thread started: ai_agent summary worker");
    
    while (!is_closed_) {
        pending_summary task;
        {
            std::unique_lock<std::mutex> lock(summary_mutex_);
            summary_cv_.wait(lock, [this] { return is_closed_ || !summary_queue_.empty(); });
            
            if (is_closed_ && summary_queue_.empty()) break;
            if (summary_queue_.empty()) continue;
            
            task = summary_queue_.front();
            summary_queue_.erase(summary_queue_.begin());
        }
        
        try {
            std::ifstream file(task.filepath);
            if (!file.is_open()) continue;
            
            nlohmann::json root;
            file >> root;
            
            if (root.contains("conversation") && root["conversation"].is_array()) {
                std::string context_dump = root["conversation"].dump(2);
                
                if (context_dump.length() < 1000) {
                    update_milestone_hint(task.milestone_id, "Trivial or extremely brief episode.");
                    continue;
                }
                
                std::string system_prompt = "You are an AI context-management assistant on a strict token budget. Below is an archived conversation 'episode' between a software engineer and an AI agent. "
                    "Write an ultra-terse 'demand-load hint' (max 1-2 sentences) so future AI agents know WHEN to retrieve this episode into their active memory. "
                    "Focus ONLY on the specific technical problems solved, files modified, and decisions made. Use highly compressed, telegraphic language to save tokens. "
                    "Start your response exactly with 'Reactivate when:' and do NOT use any conversational filler.\n\n"
                    "EPISODE JSON:\n" + context_dump;
                    
                std::vector<message> dummy_convo;
                message sys;
                sys.role = "system";
                sys.content = system_prompt;
                dummy_convo.push_back(sys);
                
                auto transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
                llm_client local_client(transport, model_->get_id(), model_->get_api_type());
                
                llm_chat_response res = local_client.send_chat(dummy_convo);
                if (!res.msg.content.empty()) {
                    update_milestone_hint(task.milestone_id, res.msg.content);
                    event_logger::get_instance().log("Generated background summary for " + task.milestone_id);
                }
            }
        } catch (const std::exception& e) {
            event_logger::get_instance().log("Error in background summarization: " + std::string(e.what()));
        }
    }
    event_logger::get_instance().log("Thread exited: ai_agent summary worker");
}

} // namespace agentlib
