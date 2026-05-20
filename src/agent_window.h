#pragma once
#include "window.h"
#include "agentlib/llm_client.h"
#include "agentlib/tool_registry.h"
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

struct agent_window_state {
    std::atomic<bool> is_closed{false};
    std::mutex conversation_mutex;
    std::vector<agentlib::message> conversation;
    std::unique_ptr<agentlib::llm_client> client;
    event_queue& global_queue;
    agentlib::document_provider* doc_provider;
    
    agent_window_state(event_queue& q, agentlib::document_provider* provider) : global_queue(q), doc_provider(provider) {}
};

class agent_window : public window {
public:
    agent_window(int id, int x, int y, int width, int height, event_queue& global_queue, agentlib::document_provider* doc_provider);
    ~agent_window();
    
    // Intercepts events from the window's local queue
    bool process_events() override; 
    void set_cursor_position() const override;
    
    // Called when the main loop dispatches the 'agent_response' event
    void append_response(const std::string& response_text);

    // Called when the main loop dispatches the 'agent_tool_update' event
    void append_tool_update(const std::string& tool_text);

    // Override to draw the input box at the bottom
    void draw_content() const; // Will need to make draw_content virtual in window.h

private:
    void submit_prompt();
    void render_input_box() const;

    std::shared_ptr<document> chat_history_;
    std::string input_buffer_;
    
    // Shared state between UI and background thread
    std::shared_ptr<agent_window_state> state_;
    
    bool is_waiting_for_llm_{false};
};
