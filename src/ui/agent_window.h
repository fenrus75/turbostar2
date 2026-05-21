#pragma once
#include "ui/window.h"
#include "agentlib/ai_agent.h"
#include <memory>

class agent_window : public window {
public:
    agent_window(int id, int x, int y, int width, int height, event_queue& global_queue, agentlib::document_provider* doc_provider);
    ~agent_window() override;
    
    // Intercepts events from the window's local queue
    bool process_events() override; 
    void set_cursor_position() const override;
    
    // Called when the main loop dispatches the 'agent_response' event
    void append_response(const std::string& response_text);

    // Called when the main loop dispatches the 'agent_tool_update' event
    void append_tool_update(const std::string& tool_text);

    // Override to draw the input box at the bottom
    void draw_content() const override; 

    std::shared_ptr<agentlib::ai_agent> get_agent() const { return agent_; }

private:
    void submit_prompt();
    void render_input_box() const;

    std::shared_ptr<document> chat_history_;
    std::string input_buffer_;
    
    std::shared_ptr<agentlib::ai_agent> agent_;
};
