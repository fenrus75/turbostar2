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

    // Create the document for the chat history
    chat_history_ = std::make_shared<document>(global_queue, "Agent Chat");
    chat_history_->set_read_only(true);
    attach_document(chat_history_);
    set_background_color_pair(17); // Use cyan background to differentiate from normal editors

    // List available skills at startup for the user
    auto& skills = skill_manager::get_instance().get_skills();
    if (!skills.empty()) {
        chat_history_->set_read_only(false);
        chat_history_->append_line("*Available Skills:*");
        for (const auto& s : skills) {
            chat_history_->append_line("- **" + s.name + "**");
        }
        chat_history_->append_line("");
        chat_history_->set_read_only(true);
        chat_history_->clear_modified();
    }
}

agent_window::~agent_window() {
    if (agent_) {
        agent_->close();
    }
}

bool agent_window::process_events() {
    bool needs_render = false;
    
    // We want to intercept key events for our input buffer.
    // If it's a key we don't care about, we push it back to a temporary queue,
    // or we just process our keys and let the base class process the rest?
    // The easiest way is to pop them all. If it's an input key, handle it.
    // If it's a scrolling key (UP, DOWN, PAGE_UP, PAGE_DOWN), pass it to the document.
    
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
                chat_history_->move_cursor(0, -1);
                needs_render = true;
            } else if (key == KEY_DOWN) {
                chat_history_->move_cursor(0, 1);
                needs_render = true;
            } else if (key == 21) { // Ctrl-U (Page up)
                chat_history_->move_page_up(get_content_height() - 3);
                needs_render = true;
            } else if (key == 22) { // Ctrl-V (Page down)
                chat_history_->move_page_down(get_content_height() - 3);
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

void agent_window::append_response(const std::string& response_text) {
    chat_history_->set_read_only(false);
    // Append response lines
    size_t start = 0;
    while (start < response_text.length()) {
        size_t end = response_text.find('\n', start);
        if (end == std::string::npos) {
            chat_history_->append_line(response_text.substr(start));
            break;
        }
        chat_history_->append_line(response_text.substr(start, end - start));
        start = end + 1;
    }

    chat_history_->append_line(""); // Spacer
    chat_history_->set_read_only(true);

    chat_history_->clear_modified();
    chat_history_->move_to_bottom();
    invalidate();
}

void agent_window::append_tool_update(const std::string& tool_text) {
    chat_history_->set_read_only(false);
    chat_history_->append_line("*Executing tool: " + tool_text + "*");
    chat_history_->set_read_only(true);

    chat_history_->clear_modified();
    chat_history_->move_to_bottom();
    invalidate();
}

void agent_window::submit_prompt() {
    std::string prompt = input_buffer_;
    input_buffer_.clear();

    chat_history_->set_read_only(false);
    chat_history_->append_line("> " + prompt);
    chat_history_->append_line("");
    chat_history_->set_read_only(true);
    
    chat_history_->clear_modified();
    chat_history_->move_to_bottom();
    invalidate();

    agent_->submit_prompt(prompt);
}

void agent_window::draw_content() const {
    // 1. Draw the chat history in the upper portion
    int input_box_height = 3; 
    
    // Temporarily shrink height so the document renderer doesn't draw over our input box
    int original_height = height_;
    const_cast<agent_window*>(this)->height_ -= input_box_height;
    
    window::draw_content(); // Render standard document
    
    const_cast<agent_window*>(this)->height_ = original_height;

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
