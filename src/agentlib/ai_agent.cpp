#include "ai_agent.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../config_manager.h"
#include "../event_queue.h"
#include "../git_manager.h"
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
	return std::shared_ptr<ai_agent>(new ai_agent(id, name, std::move(model), queue, doc_provider));
}

ai_agent::ai_agent(int id, const std::string &name, std::shared_ptr<ai_model> model, event_queue *queue, document_provider *doc_provider)
    : id_(id), name_(name), model_(std::move(model)), global_queue_(queue), doc_provider_(doc_provider)
{
	auto http_transport = std::make_shared<httplib_transport>(model_->get_url(), model_->get_api_key());
	client_ = std::make_unique<llm_client>(http_transport, model_->get_id(), model_->get_api_type());
}

ai_agent::~ai_agent()
{
	close();
}

void ai_agent::close()
{
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

bool ai_agent::mark_todo_complete(const std::string &text_match, std::string &out_error)
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
			if (self->is_closed_)
				return;

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
					    for (const auto &tc : *delta.tool_calls) {
						    if (tc.id.empty() && !accumulated_tool_calls.empty()) {
							    auto &last = accumulated_tool_calls.back();
							    if (!tc.function.name.empty())
								    last.function.name += tc.function.name;
							    if (!tc.function.arguments.empty())
								    last.function.arguments += tc.function.arguments;
						    } else {
							    accumulated_tool_calls.push_back(tc);
						    }
					    }
				    }

				    if (delta.usage.total_tokens > 0) {
					    self->tokens_tx_ += delta.usage.prompt_tokens;
					    self->tokens_rx_ += delta.usage.completion_tokens;

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

			if (self->is_closed_)
				return;

			// Commit assistant response to shared history
			{
				std::lock_guard<std::mutex> lock(self->conversation_mutex_);
				self->conversation_.push_back(response_msg);
				last_synced_index = self->conversation_.size();
			}
			convo.push_back(response_msg);

			if (!accumulated_tool_calls.empty()) {
				for (const auto &call : accumulated_tool_calls) {
					if (self->is_closed_)
						return;

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
				}
				self->current_tool_.clear();
				self->set_status(agent_status::thinking);
			} else {
				final_response = response_msg.content;
				break;
			}
		}

		if (self->is_closed_)
			return;

		self->set_status(agent_status::idle);

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
							    {"agent_id", self->id_},
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

} // namespace agentlib