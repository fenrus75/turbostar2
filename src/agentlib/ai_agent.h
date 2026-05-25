#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include "llm_client.h"
#include "tool_registry.h"
#include "document_provider.h"
#include "interactions/interactions.h"
#include "ai_model.h"

class event_queue;

namespace agentlib {

enum class agent_status {
    idle,
    thinking,
    tool_execution,
    waiting,
    error
};

std::string agent_status_to_string(agent_status status, const std::string& tool_name = "");

struct todo_item {
    std::string text;
    bool completed{false};
};

class ai_agent : public std::enable_shared_from_this<ai_agent> {
public:
    static std::shared_ptr<ai_agent> create(int id, const std::string& name, std::shared_ptr<ai_model> model, event_queue* queue, document_provider* doc_provider);
    ~ai_agent();

    void submit_prompt(const std::string& prompt_text);
    void inject_context(const std::string& role, const std::string& content, bool trigger_processing = false);
    void cancel_current_task();
    void close();

    int get_id() const { return id_; }
    std::string get_name() const { return name_; }
    agent_status get_status() const { return status_; }
    std::string get_current_tool() const { return current_tool_; }
    
    // Explicitly set the status, optionally with a target ID if waiting
    void set_status(agent_status s, int target_id = -1);
    
    // Blocks until the agent's status is idle or error
    void wait_until_idle();

    int get_waiting_on_id() const { return waiting_on_id_; }

    void add_todo(const std::string& task);
    std::vector<todo_item> get_todos() const;
    std::optional<std::string> pop_todo();
    bool mark_todo_complete(const std::string& text_match, std::string& out_error);
    bool delete_todo(const std::string& text_match, std::string& out_error);

    std::shared_ptr<ai_agent> spawn_subagent(const std::string& task_description);
    void remove_subagent(int id);
    std::vector<std::shared_ptr<ai_agent>> get_subagents() const;

    void set_model(std::shared_ptr<ai_model> model);
    std::shared_ptr<ai_model> get_model() const { return model_; }
    int get_tokens_tx() const { return tokens_tx_; }
    int get_tokens_rx() const { return tokens_rx_; }
    int get_tokens_cached() const { return tokens_cached_; }
    double get_estimated_cost() const { return estimated_cost_; }
    
    void add_active_skill(const std::string& skill_name);
    std::vector<std::string> get_active_skills() const;

    const std::vector<std::shared_ptr<agent_interaction>>& get_interactions() const { return interactions_; }
    void add_interaction(std::shared_ptr<agent_interaction> interaction);

    bool is_read_only() const { return read_only_; }
    void set_read_only(bool ro) { read_only_ = ro; }

    void set_parent(std::weak_ptr<ai_agent> parent) { parent_agent_ = std::move(parent); }
    event_queue* get_global_queue() const { return global_queue_; }

    void save_conversation(const std::string& filepath) const;

private:
    ai_agent(int id, const std::string& name, std::shared_ptr<ai_model> model, event_queue* queue, document_provider* doc_provider);

    void start_processing();
    void compact_ephemeral_errors(std::vector<message>& convo);

    int id_;
    std::string name_;
    std::shared_ptr<ai_model> model_;
    std::atomic<agent_status> status_{agent_status::idle};
    std::string current_tool_;
    std::atomic<bool> is_closed_{false};
    std::atomic<bool> read_only_{false};
    std::atomic<int> waiting_on_id_{-1};

    std::weak_ptr<ai_agent> parent_agent_;

    event_queue* global_queue_;
    document_provider* doc_provider_;

    std::mutex state_mutex_;
    std::condition_variable status_cv_;
    std::vector<todo_item> todos_;
    std::vector<std::shared_ptr<ai_agent>> subagents_;
    std::vector<std::string> active_skills_;
    std::vector<std::shared_ptr<agent_interaction>> interactions_;

    std::atomic<int> tokens_tx_{0};
    std::atomic<int> tokens_rx_{0};
    std::atomic<int> tokens_cached_{0};
    std::atomic<double> estimated_cost_{0.0};

    mutable std::mutex conversation_mutex_;
    std::vector<message> conversation_;
    std::unique_ptr<llm_client> client_;
};

} // namespace agentlib