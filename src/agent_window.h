#pragma once
#include "window.h"
#include "agentlib/llm_client.h"
#include "agentlib/tool_registry.h"
#include <vector>
#include <thread>

#include <mutex>

class agent_window : public window {
public:
    agent_window(int id, int x, int y, int width, int height, event_queue& global_queue);
    ~agent_window();
    
    // Intercepts events from the window's local queue
    bool process_events() override; 
    
    // Called when the main loop dispatches the 'agent_response' event
    void append_response(const std::string& response_text);

    // Override to draw the input box at the bottom
    void draw_content() const; // Will need to make draw_content virtual in window.h

private:
    void submit_prompt();
    void render_input_box() const;

    std::shared_ptr<document> chat_history_;
    std::string input_buffer_;
    
    // Agent state
    std::unique_ptr<agentlib::llm_client> client_;
    std::vector<agentlib::message> conversation_;
    mutable std::mutex conversation_mutex_;
    
    event_queue& global_queue_;
    bool is_waiting_for_llm_{false};
};
