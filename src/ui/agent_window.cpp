#include "ui/agent_window.h"
#include "config_manager.h"
#include "git_manager.h"
#include "agentlib/httplib_transport.h"
#include "agentlib/skill_manager.h"
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include <algorithm>

using namespace agentlib;

agent_window::agent_window(int id, int x, int y, int width, int height, event_queue& global_queue, agentlib::document_provider* doc_provider)
    : window(id, x, y, width, height, "Agent Chat")
{
    std::string url = config_manager::get_instance().get_llm_url();
    agent_ = ai_agent::create(id, "Agent", url, &global_queue, doc_provider);

    set_background_color_pair(17); // Use cyan background to differentiate from normal editors

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
                }
                continue; // Ignore input while waiting
            }

            if (key == 10 || key == 13 || key == KEY_ENTER) {
                if (!input_buffer_.empty()) {
                    submit_prompt();
                    needs_render = true;
                }
            } else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
                if (!input_buffer_.empty()) {
                    input_buffer_.pop_back();
                    needs_render = true;
                }
            } else if (key == KEY_UP) {
                scroll_offset_++;
                needs_render = true;
            } else if (key == KEY_DOWN) {
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
            } else if (key >= 32 && key <= 126) {
                input_buffer_ += static_cast<char>(key);
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

void agent_window::submit_prompt() {
    std::string prompt = input_buffer_;
    input_buffer_.clear();
    scroll_offset_ = 0;
    invalidate();
    agent_->submit_prompt(prompt);
}

void agent_window::draw_content() const {
    // 1. Draw the chat history in the upper portion
    int input_box_height = 3; 
    int available_height = height_ - 2 - input_box_height;
    int current_y = y_ + height_ - 2 - input_box_height - 1; // start from bottom of available area
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
    render_input_box();
}

void agent_window::set_cursor_position() const {
    if (!(agent_->get_status() != agent_status::idle)) {
        int input_box_y = y_ + height_ - 4;
        int input_box_x = x_ + 1;
        int max_display_len = width_ - 5;
        
        std::string display_text = input_buffer_;
        if (display_text.length() > static_cast<size_t>(max_display_len)) {
            display_text = display_text.substr(display_text.length() - max_display_len);
        }
        
        move(input_box_y + 1, input_box_x + 2 + display_text.length());
    }
}

void agent_window::render_input_box() const {
    int input_box_y = y_ + height_ - 4; // Above the bottom border
    int input_box_x = x_ + 1;
    int input_box_w = width_ - 2;

    attrset(COLOR_PAIR(1)); // Normal text for input box
    
    // Draw separator line
    for (int i = 0; i < input_box_w; ++i) {
        mvaddch(input_box_y, input_box_x + i, ACS_HLINE);
    }

    // Draw prompt indicator
    mvaddstr(input_box_y + 1, input_box_x, "> ");
    
    // Draw input buffer (handle horizontal scrolling simply for now)
    std::string display_text = input_buffer_;
    int max_display_len = input_box_w - 3;
    if (display_text.length() > static_cast<size_t>(max_display_len)) {
        display_text = display_text.substr(display_text.length() - max_display_len);
    }
    
    mvaddstr(input_box_y + 1, input_box_x + 2, display_text.c_str());

    // Fill rest with spaces
    for (size_t i = display_text.length(); i < static_cast<size_t>(max_display_len); ++i) {
        addch(' ');
    }
    
    // Status text
    if ((agent_->get_status() != agent_status::idle)) {
        attrset(COLOR_PAIR(15)); // Highlight
        mvaddstr(input_box_y + 2, input_box_x, " Waiting for LLM response... ");
    } else {
        attrset(COLOR_PAIR(10)); // Muted
        mvaddstr(input_box_y + 2, input_box_x, " Press ENTER to send. ");
    }
    attrset(0);
}
