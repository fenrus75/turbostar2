#include "ui/agent_window.h"
#include "config_manager.h"
#include "project_manager.h"
#include "git_manager.h"
#include "agentlib/httplib_transport.h"
#include "agentlib/skill_manager.h"
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include <algorithm>

using namespace agentlib;

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_model> model, event_queue& global_queue, agentlib::document_provider* doc_provider)
    : window(id, x, y, width, height, "Agent Chat")
{
    agent_ = ai_agent::create(id, "Agent", std::move(model), &global_queue, doc_provider);

    std::string project_instr = project_manager::get_instance().get_project_instructions();
    if (!project_instr.empty()) {
        agent_->inject_context("system", "Project-specific instructions and engineering standards:\n" + project_instr);
    }

    set_background_color_pair(17); // Use cyan background to differentiate from normal editors

    input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this](const std::string& text) {
        agent_->submit_prompt(text);
        scroll_offset_ = 0;
        invalidate();
    });

    // List available skills at startup for the user
    auto& skills = skill_manager::get_instance().get_skills();
    if (!skills.empty()) {
        std::string skills_text = "Available Skills:\n";
        for (const auto& s : skills) {
            skills_text += "- " + s.name + "\n";
        }
        agent_->add_interaction(std::make_shared<agentlib::interaction_system_message>(skills_text));
    }
}

agent_window::agent_window(int id, int x, int y, int width, int height, std::shared_ptr<agentlib::ai_agent> existing_agent, event_queue& global_queue)
    : window(id, x, y, width, height, existing_agent->get_name()), agent_(std::move(existing_agent))
{
    set_background_color_pair(17);

    input_box_ = std::make_unique<ui_multiline_edit>("input", 0, 0, width_ - 2, 3, [this](const std::string& text) {
        agent_->submit_prompt(text);
        scroll_offset_ = 0;
        invalidate();
    });
}

agent_window::~agent_window() {
    if (agent_) {
        agent_->close();
    }
}

bool agent_window::process_events() {
    bool needs_render = false;
    
    while (auto ev = get_queue().pop()) {
        if (ev->type == event_type::key_press) {
            int key = ev->key_code;
            
            if ((agent_->get_status() != agent_status::idle)) {
                if (key == 27) {
                    agent_->cancel_current_task();
                    needs_render = true;
                }
                continue; // Ignore input while waiting
            }

            if (is_active() && input_box_ && input_box_->handle_event(*ev, 0, 0)) {
                needs_render = true;
                continue;
            }

            // Window-level scrolling fallback
            if (key == KEY_UP) { // Up
                scroll_offset_++;
                needs_render = true;
            } else if (key == KEY_DOWN) { // Down
                if (scroll_offset_ > 0) {
                    scroll_offset_--;
                    needs_render = true;
                }
            } else if (key == 21) { // Ctrl-U (Page up)
                scroll_offset_ += get_content_height() - 3;
                needs_render = true;
            } else if (key == 22) { // Ctrl-V (Page down)
                scroll_offset_ -= get_content_height() - 3;
                if (scroll_offset_ < 0) scroll_offset_ = 0;
                needs_render = true;
            }
        }
    }
    
    if (needs_render) invalidate();
    return needs_render;
}

void agent_window::on_agent_update() {
    scroll_offset_ = 0; // Snap to bottom
    invalidate();
}

void agent_window::draw_content() const {
    // 1. Draw the chat history in the upper portion
    int input_box_height = 3; 
    int separator_height = 1;
    int reserved_bottom = input_box_height + separator_height;
    int available_height = height_ - 2 - reserved_bottom;
    int current_y = y_ + 1 + available_height - 1; // start from bottom of available area
    int start_x = x_ + 1;
    int max_width = width_ - 2;

    attron(COLOR_PAIR(get_background_color_pair()));

    // First, clear the content area
    for (int i = 1; i <= available_height; ++i) {
        move(y_ + i, x_ + 1);
        for (int j = 0; j < width_ - 2; ++j) addch(' ');
    }

    auto interactions = agent_->get_interactions();
    
    // We render backwards from the bottom
    int rendered_lines = 0;
    
    // Apply scroll offset
    int skip_lines = scroll_offset_;

    for (auto it = interactions.rbegin(); it != interactions.rend() && rendered_lines < available_height; ++it) {
        auto lines = (*it)->render(max_width);
        
        // Iterate backwards through the lines of this interaction
        for (auto line_it = lines.rbegin(); line_it != lines.rend(); ++line_it) {
            if (skip_lines > 0) {
                skip_lines--;
                continue;
            }
            
            if (rendered_lines >= available_height) break;

            attron(COLOR_PAIR(line_it->color_pair));
            
            std::string display_text = line_it->text;
            if (display_text.length() > static_cast<size_t>(max_width)) {
                display_text = display_text.substr(0, max_width);
            }
            
            mvprintw(current_y, start_x, "%s", display_text.c_str());
            attroff(COLOR_PAIR(line_it->color_pair));
            
            attron(COLOR_PAIR(get_background_color_pair())); // restore bg
            
            current_y--;
            rendered_lines++;
        }
    }

    attroff(COLOR_PAIR(get_background_color_pair()));

    // 2. Draw the input box at the bottom
    if (input_box_) {
        // Draw separator line
        int input_box_y = y_ + height_ - 4; 
        int separator_y = input_box_y - 1;

        attrset(COLOR_PAIR(get_background_color_pair()));
        for (int i = 0; i < max_width; ++i) {
            mvaddch(separator_y, start_x + i, ACS_HLINE);
        }
        
        input_box_->set_bounds(start_x, input_box_y, max_width, 3);
        input_box_->set_focus(is_active());
        input_box_->draw(0, 0);
    }
}

void agent_window::set_cursor_position() const {
    if (is_active() && input_box_) {
        input_box_->draw(0, 0);
    }
}
