#include "agent_status_window.h"
#include "agentlib/skill_manager.h"
#include <ncurses.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

agent_status_window::agent_status_window(int id, int x, int y, int width, int height, const std::string& title, std::shared_ptr<agentlib::ai_agent> agent, event_queue& global_queue)
    : window(id, x, y, width, height, title), agent_(std::move(agent)), global_queue_(global_queue) {
    // 2 represents COLOR_WINDOW from standard macros in project.
    set_background_color_pair(2);

    subagents_list_ = std::make_unique<ui_listbox>("subagents", 0, 0, width_ - 2, 1,
        [this](int) {
            invalidate(); // redraw on selection change
        },
        [this](int index) {
            auto subagents = agent_->get_subagents();
            if (index >= 0 && index < (int)subagents.size()) {
                editor_event sub_ev;
                sub_ev.type = event_type::open_subagent;
                sub_ev.key_code = subagents[index]->get_id();
                global_queue_.push(sub_ev);
            }
        });
}

void agent_status_window::draw_content() const {
    if (!is_visible()) {
        return;
    }

    int current_y = y_ + 1;
    int start_x = x_ + 1;
    int max_width = width_ - 2;

    attron(COLOR_PAIR(get_background_color_pair()));

    // First, clear the content area
    for (int i = 1; i < height_ - 1; ++i) {
        move(y_ + i, x_ + 1);
        for (int j = 0; j < width_ - 2; ++j) addch(' ');
    }

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
    auto active_skills = agent_->get_active_skills();
    auto all_skills = agentlib::skill_manager::get_instance().get_skills();
    
    if (all_skills.empty()) {
        print_line(start_x, current_y++, "  None available");
    } else {
        for (const auto& skill : all_skills) {
            if (current_y < y_ + height_ - 1) {
                bool is_active = std::find(active_skills.begin(), active_skills.end(), skill.name) != active_skills.end();
                // E26x91x92 is \xE2\x98\x91 BALLOT BOX WITH CHECK ☑
                // E26x98x90 is \xE2\x98\x90 BALLOT BOX ☐
                std::string box = is_active ? "\xE2\x98\x91" : "\xE2\x98\x90";
                print_line(start_x, current_y++, " " + box + " " + skill.name);
            }
        }
    }

    current_y++; // Spacing

    // 3. Subagents Section
    if (current_y < y_ + height_ - 1) {
        print_line(start_x, current_y++, "--- Subagents ---");
        
        auto subagents = agent_->get_subagents();
        std::vector<std::string> subagent_strings;
        for (const auto& sub : subagents) {
            std::string status_str;
            switch (sub->get_status()) {
                case agentlib::agent_status::idle: status_str = "[Idle]"; break;
                case agentlib::agent_status::thinking: status_str = "[Thinking]"; break;
                case agentlib::agent_status::tool_execution: status_str = "[Tool]"; break;
                case agentlib::agent_status::error: status_str = "[Error]"; break;
            }
            subagent_strings.push_back(" " + sub->get_name() + " " + status_str);
        }
        
        // Compute dynamic height available
        int available_height = (y_ + height_ - 1) - current_y;
        if (available_height > 0) {
            subagents_list_->set_bounds(start_x, current_y, max_width, available_height);
            subagents_list_->set_items(subagent_strings);
            
            // Pass focus state to the listbox
            subagents_list_->set_focus(is_active());
        }
    }
    
    attroff(COLOR_PAIR(get_background_color_pair()));

    // Draw the listbox
    if (subagents_list_) {
        // draw uses absolute coordinates. We pass 0,0 since set_bounds used start_x/current_y as relative to 0,0
        // Wait, set_bounds sets x_, y_. If we pass abs_x=0, abs_y=0, it will draw at x_, y_.
        subagents_list_->draw(0, 0);
    }
}

bool agent_status_window::process_events() {
    bool needs_render = false;
    while (auto ev = get_window_queue().pop()) {
        if (is_active() && subagents_list_ && subagents_list_->handle_event(*ev, 0, 0)) {
            needs_render = true;
            invalidate();
            continue;
        }
        // ... (handle other window keys if any, none for now)
    }
    
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