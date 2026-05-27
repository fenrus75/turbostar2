#pragma once

#include "window.h"
#include "../agentlib/ai_agent.h"
#include "ui/components/ui_listbox.h"
#include <memory>

class agent_status_window : public window {
public:
    agent_status_window(int id, int x, int y, int width, int height, const std::string& title, std::shared_ptr<agentlib::ai_agent> agent, event_queue& global_queue);
    ~agent_status_window() override = default;

    void draw_content() const override;
    bool process_events() override;
    void set_cursor_position() const override;

    void set_agent(std::shared_ptr<agentlib::ai_agent> agent) { agent_ = std::move(agent); invalidate(); }
    std::shared_ptr<agentlib::ai_agent> get_agent() const { return agent_; }

private:
    std::shared_ptr<agentlib::ai_agent> agent_;
    event_queue& global_queue_;
    int cursor_y_{0};
    int scroll_offset_{0};
    
    std::unique_ptr<ui_listbox> subagents_list_;
};