#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include "llm_client.h"
#include "tool_registry.h"
#include "document_provider.h"

class event_queue;

namespace agentlib {

enum class agent_status {
    idle,
    thinking,
    tool_execution,
    error
};

struct todo_item {
    std::string text;
    bool completed{false};
};

class ai_agent : public std::enable_shared_from_this<ai_agent> {
public:
    static std::shared_ptr<ai_agent> create(int id, const std::string& name, const std::string& llm_url, event_queue* queue, document_provider* doc_provider);
    ~ai_agent();

    void submit_prompt(const std::string& prompt_text);
    void cancel_current_task();
    void close();

    int get_id() const { return id_; }
    std::string get_name() const { return name_; }
    agent_status get_status() const { return status_; }

    void add_todo(const std::string& task);
    std::vector<todo_item> get_todos() const;
    bool mark_todo_complete(const std::string& text_match, std::string& out_error);
    bool delete_todo(const std::string& text_match, std::string& out_error);

    std::shared_ptr<ai_agent> spawn_subagent(const std::string& task_description);
    const std::vector<std::shared_ptr<ai_agent>>& get_subagents() const { return subagents_; }

private:
    ai_agent(int id, const std::string& name, const std::string& llm_url, event_queue* queue, document_provider* doc_provider);

    int id_;
    std::string name_;
    std::atomic<agent_status> status_{agent_status::idle};
    std::atomic<bool> is_closed_{false};

    event_queue* global_queue_;
    document_provider* doc_provider_;

    std::mutex state_mutex_;
    std::vector<todo_item> todos_;
    std::vector<std::shared_ptr<ai_agent>> subagents_;

    std::mutex conversation_mutex_;
    std::vector<message> conversation_;
    std::unique_ptr<llm_client> client_;
};

} // namespace agentlib