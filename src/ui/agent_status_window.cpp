#include "agent_status_window.h"
#include <ncurses.h>
#include <iomanip>
#include <sstream>

agent_status_window::agent_status_window(int id, int x, int y, int width, int height, const std::string& title, std::shared_ptr<agentlib::ai_agent> agent)
    : window(id, x, y, width, height, title), agent_(std::move(agent)) {
    // 2 represents COLOR_WINDOW from standard macros in project.
    set_background_color_pair(2);
}

void agent_status_window::draw() const {
    window::draw();

    if (!is_visible()) {
        return;
    }

    int current_y = y_ + 1;
    int start_x = x_ + 1;
    int max_width = width_ - 2;

    attron(COLOR_PAIR(get_background_color_pair()));

    auto print_line = [&](int x, int y, const std::string& text) {
        if (text.length() > (size_t)max_width) {
            mvprintw(y, x, "%s", text.substr(0, max_width).c_str());
        } else {
            mvprintw(y, x, "%s", text.c_str());
        }
    };

    if (!agent_) {
        print_line(start_x, current_y++, "No Active Agent");
        attroff(COLOR_PAIR(get_background_color_pair()));
        return;
    }

    // 1. Telemetry Section
    print_line(start_x, current_y++, "Model: " + agent_->get_model_name());
    
    std::stringstream tx_stream;
    tx_stream << "Tx: " << agent_->get_tokens_tx() << " tokens";
    print_line(start_x, current_y++, tx_stream.str());
    
    std::stringstream rx_stream;
    rx_stream << "Rx: " << agent_->get_tokens_rx() << " tokens";
    print_line(start_x, current_y++, rx_stream.str());

    std::stringstream cost_stream;
    cost_stream << std::fixed << std::setprecision(4) << "Cost: $" << agent_->get_estimated_cost();
    print_line(start_x, current_y++, cost_stream.str());

    current_y++; // Spacing

    // 2. Skills Section
    print_line(start_x, current_y++, "--- Skills ---");
    auto skills = agent_->get_active_skills();
    if (skills.empty()) {
        print_line(start_x, current_y++, "  None active");
    } else {
        for (const auto& skill : skills) {
            if (current_y < y_ + height_ - 1) {
                // E26x91x92 is \xE2\x98\x91 BALLOT BOX WITH CHECK ☑
                print_line(start_x, current_y++, " \xE2\x98\x91 " + skill);
            }
        }
    }

    current_y++; // Spacing

    // 3. Subagents Section
    if (current_y < y_ + height_ - 1) {
        print_line(start_x, current_y++, "--- Subagents ---");
        auto subagents = agent_->get_subagents();
        if (subagents.empty()) {
            print_line(start_x, current_y++, "  None");
        } else {
            for (const auto& sub : subagents) {
                if (current_y < y_ + height_ - 1) {
                    std::string status_str;
                    switch (sub->get_status()) {
                        case agentlib::agent_status::idle: status_str = "[Idle]"; break;
                        case agentlib::agent_status::thinking: status_str = "[Thinking]"; break;
                        case agentlib::agent_status::tool_execution: status_str = "[Tool]"; break;
                        case agentlib::agent_status::error: status_str = "[Error]"; break;
                    }
                    print_line(start_x, current_y++, " " + sub->get_name() + " " + status_str);
                }
            }
        }
    }
    
    attroff(COLOR_PAIR(get_background_color_pair()));
}

bool agent_status_window::process_events() {
    bool needs_render = window::process_events();
    // TODO: Handle keyboard navigation here (Up/Down arrows, Enter to view details)
    return needs_render;
}

void agent_status_window::set_cursor_position() const {
    if (is_active()) {
        int target_y = y_ + 1 + cursor_y_;
        if (target_y > y_ + height_ - 2) target_y = y_ + height_ - 2;
        move(target_y, x_ + 1);
    } else {
        window::set_cursor_position();
    }
}