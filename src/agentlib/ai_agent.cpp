#include "ai_agent.h"
#include "httplib_transport.h"
#include "skill_manager.h"
#include "../git_manager.h"
#include "../event_queue.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>

namespace agentlib {

std::shared_ptr<ai_agent> ai_agent::create(int id, const std::string& name, const std::string& llm_url, event_queue* queue, document_provider* doc_provider) {
    return std::shared_ptr<ai_agent>(new ai_agent(id, name, llm_url, queue, doc_provider));
}

ai_agent::ai_agent(int id, const std::string& name, const std::string& llm_url, event_queue* queue, document_provider* doc_provider)
    : id_(id), name_(name), llm_url_(llm_url), global_queue_(queue), doc_provider_(doc_provider) 
{
    auto http_transport = std::make_shared<httplib_transport>(llm_url);
    client_ = std::make_unique<llm_client>(http_transport);
}

ai_agent::~ai_agent() {
    close();
}

void ai_agent::close() {
    is_closed_ = true;
    cancel_current_task();
}

void ai_agent::cancel_current_task() {
    if (client_) {
        client_->cancel();
    }
}

void ai_agent::add_todo(const std::string& task) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    todos_.push_back({task, false});
}

std::vector<todo_item> ai_agent::get_todos() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return todos_;
}

void ai_agent::add_active_skill(const std::string& skill_name) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // Check if it already exists to avoid duplicates
    if (std::find(active_skills_.begin(), active_skills_.end(), skill_name) == active_skills_.end()) {
        active_skills_.push_back(skill_name);
    }
}

std::vector<std::string> ai_agent::get_active_skills() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return active_skills_;
}

bool ai_agent::mark_todo_complete(const std::string& text_match, std::string& out_error) {
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

bool ai_agent::delete_todo(const std::string& text_match, std::string& out_error) {
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

std::shared_ptr<ai_agent> ai_agent::spawn_subagent(const std::string& name) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    int new_id = id_ * 100 + static_cast<int>(subagents_.size()) + 1;
    auto subagent = ai_agent::create(new_id, name, llm_url_, global_queue_, doc_provider_);
    subagent->set_parent(shared_from_this());
    subagents_.push_back(subagent);
    return subagent;
}

void ai_agent::remove_subagent(int id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    subagents_.erase(std::remove_if(subagents_.begin(), subagents_.end(),
        [id](const std::shared_ptr<ai_agent>& agent) {
            return agent->get_id() == id;
        }), subagents_.end());
}

std::vector<std::shared_ptr<ai_agent>> ai_agent::get_subagents() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(state_mutex_));
    return subagents_;
}

void ai_agent::add_interaction(std::shared_ptr<agent_interaction> interaction) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    interactions_.push_back(std::move(interaction));
}

void ai_agent::inject_context(const std::string& role, const std::string& content) {
    {
        std::lock_guard<std::mutex> lock(conversation_mutex_);
        message context_msg;
        context_msg.role = role;
        context_msg.content = content;
        conversation_.push_back(context_msg);
    }
    
    add_interaction(std::make_shared<interaction_system_message>(content));

    // If the agent is idle or waiting, wake it up to process this new context.
    if (status_ == agent_status::idle || status_ == agent_status::waiting) {
        // To wake it up safely, we can just call an empty submit_prompt.
        // Wait, submit_prompt adds a user message if it's not empty, and starts the thread.
        // Let's refactor the thread starting logic into a private method or just reuse submit_prompt("").
        submit_prompt("");
    }
}

void ai_agent::submit_prompt(const std::string& prompt_text) {
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

    status_ = agent_status::thinking;

    std::thread([self = shared_from_this()]() {
        std::vector<message> convo_copy;
        {
            std::lock_guard<std::mutex> lock(self->conversation_mutex_);
            convo_copy = self->conversation_;
        }
        
        auto& registry = tool_registry::get_instance();
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
            if (self->is_closed_) return;

            self->status_ = agent_status::thinking;
            llm_chat_response chat_res = self->client_->send_chat(convo_copy, &registry);
            message response = chat_res.msg;

            // Accumulate usage and update model name
            self->tokens_tx_ += chat_res.usage.prompt_tokens;
            self->tokens_rx_ += chat_res.usage.completion_tokens;
            if (!chat_res.model.empty()) {
                self->model_name_ = chat_res.model;
            }
            
            // Simple placeholder cost calculation
            double cost_per_1m_tx = 5.00; // GPT-4o placeholder
            double cost_per_1m_rx = 15.00;
            if (self->model_name_.find("gemini") != std::string::npos) {
                cost_per_1m_tx = 3.50; // Gemini 1.5 Pro placeholder
                cost_per_1m_rx = 10.50;
            } else if (self->model_name_.find("claude") != std::string::npos) {
                cost_per_1m_tx = 3.00;
                cost_per_1m_rx = 15.00;
            }
            
            double current_cost = self->estimated_cost_.load();
            current_cost += (chat_res.usage.prompt_tokens / 1000000.0) * cost_per_1m_tx;
            current_cost += (chat_res.usage.completion_tokens / 1000000.0) * cost_per_1m_rx;
            self->estimated_cost_.store(current_cost);

            if (self->is_closed_) return;
            if (response.tool_calls && !response.tool_calls->empty()) {
                self->status_ = agent_status::tool_execution;
                convo_copy.push_back(response);

                for (const auto& call : *response.tool_calls) {
                    if (self->is_closed_) return;

                    std::string arg_preview;
                    try {
                        auto args_json = nlohmann::json::parse(call.function.arguments);
                        if (args_json.is_object()) {
                            bool first = true;
                            for (const auto& item : args_json.items()) {
                                if (!first) arg_preview += ", ";
                                first = false;
                                
                                std::string val_str;
                                if (item.value().is_string()) {
                                    std::string s = item.value().get<std::string>();
                                    std::replace(s.begin(), s.end(), '\n', ' ');
                                    std::replace(s.begin(), s.end(), '\r', ' ');
                                    if (s.length() > 40) s = s.substr(0, 37) + "...";
                                    val_str = "\"" + s + "\"";
                                } else {
                                    std::string s = item.value().dump();
                                    if (s.length() > 40) s = s.substr(0, 37) + "...";
                                    val_str = s;
                                }
                                arg_preview += val_str;
                            }
                        }
                    } catch (...) {
                        arg_preview = "...";
                    }

                    bool is_silent = registry.is_tool_silent(call.function.name);

                    if (!is_silent) {
                        self->add_interaction(std::make_shared<interaction_tool_call>(call.function.name + "(" + arg_preview + ")"));
                        if (self->global_queue_) {
                            editor_event tool_ev;
                            tool_ev.type = event_type::agent_tool_update;
                            tool_ev.key_code = self->id_;
                            // Payload no longer needed, window can just pull from interactions_
                            self->global_queue_->push(tool_ev);
                        }
                    }

                    std::string tool_result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
                    
                    std::string result_preview = tool_result;
                    if (result_preview.find("Stage 1 Security Violation:") != 0 && 
                        result_preview.find("Execution Error:") != 0 &&
                        result_preview.find("Error parsing tool arguments:") != 0) {
                        size_t newline_pos = result_preview.find('\n');
                        if (newline_pos != std::string::npos) {
                            result_preview = result_preview.substr(0, newline_pos) + " ...";
                        }
                        if (result_preview.length() > 300) {
                            result_preview = result_preview.substr(0, 297) + "...";
                        }
                    }
                    
                    if (!is_silent) {
                        self->add_interaction(std::make_shared<interaction_tool_result>(result_preview));
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
                    
                    convo_copy.push_back(tool_msg);
                }
            } else {
                convo_copy.push_back(response);
                final_response = response.content;
                break;
            }
        }
        
        if (self->is_closed_) return;

        {
            std::lock_guard<std::mutex> lock(self->conversation_mutex_);
            self->conversation_ = convo_copy;
        }
        
        self->status_ = agent_status::idle;

        if (!final_response.empty()) {
            self->add_interaction(std::make_shared<interaction_llm_response>(final_response));
        }

        if (self->global_queue_) {
            editor_event ev;
            ev.type = event_type::agent_response;
            ev.key_code = self->id_;
            ev.payload = final_response;
            self->global_queue_->push(ev);
        }

        // Notify parent agent asynchronously
        if (auto parent = self->parent_agent_.lock()) {
            parent->inject_context("system", "Subagent " + std::to_string(self->id_) + " (" + self->name_ + ") has finished processing and returned to idle state. Use the agent_get_output(" + std::to_string(self->id_) + ") tool to retrieve its interaction history and results.");
        }
    }).detach();
}

} // namespace agentlib