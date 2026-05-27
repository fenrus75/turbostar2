#pragma once
#include "ui/window.h"
#include "agentlib/ai_agent.h"
#include "ui/components/ui_multiline_edit.h"
#include <memory>

class agent_window : public window {
public:
    agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_model> model, event_queue& global_queue, agentlib::document_provider* doc_provider, bool fresh_agent = false);
    agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_agent> existing_agent);
    ~agent_window() override;
    
    // Intercepts events from the window's local queue
    bool process_events() override; 
    void set_cursor_position() const override;
    
    // Called when the main loop dispatches an agent event
    void on_agent_update();

    // Override to draw the input box at the bottom
    void draw_content() const override; 

    std::shared_ptr<agentlib::ai_agent> get_agent() const { return agent_; }

private:
    int scroll_offset_{0};
    
    std::unique_ptr<ui_multiline_edit> input_box_;
    std::shared_ptr<agentlib::ai_agent> agent_;
};
