#include "agent_window.h"
#include "config_manager.h"
#include "agentlib/httplib_transport.h"
#include <ncurses.h>

using namespace agentlib;

agent_window::agent_window(int id, int x, int y, int width, int height, event_queue& global_queue)
    : window(id, x, y, width, height, "Agent Chat"), global_queue_(global_queue) 
{
    // Create the document for the chat history
    chat_history_ = std::make_shared<document>(global_queue_);
    attach_document(chat_history_);
    set_background_color_pair(17); // Use cyan background to differentiate from normal editors

    std::string url = config_manager::get_instance().get_llm_url();
    auto http_transport = std::make_shared<httplib_transport>(url);
    client_ = std::make_unique<llm_client>(http_transport);
}

agent_window::~agent_window() = default;

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
            
            if (is_waiting_for_llm_) {
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
    is_waiting_for_llm_ = false;

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
    
    // Auto-scroll to bottom
    chat_history_->move_to_bottom();
    invalidate();
}

void agent_window::append_tool_update(const std::string& tool_text) {
    chat_history_->append_line("*Executing tool: " + tool_text + "*");
    chat_history_->move_to_bottom();
    invalidate();
}

void agent_window::submit_prompt() {
    std::string prompt = input_buffer_;
    input_buffer_.clear();
    is_waiting_for_llm_ = true;

    // Echo to history
    chat_history_->append_line("> " + prompt);
    chat_history_->append_line("");
    chat_history_->move_to_bottom();
    invalidate();

    // Add to conversation state
    {
        std::lock_guard<std::mutex> lock(conversation_mutex_);
        message user_msg;
        user_msg.role = "user";
        user_msg.content = prompt;
        conversation_.push_back(user_msg);
    }

    // Spawn thread
    std::thread([this]() {
        // Copy conversation to avoid race conditions with the UI thread
        std::vector<message> convo_copy;
        {
            std::lock_guard<std::mutex> lock(conversation_mutex_);
            convo_copy = conversation_;
        }
        
        // Use the global registry for tools
        auto& registry = tool_registry::get_instance();
        
        // Setup tool context
        tool_context ctx;
        ctx.fs_security.set_working_directory(std::filesystem::current_path());
        ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::read);
        ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::write);
        
        std::string final_response;

        while (true) {
            // Send request (blocks thread, UI stays responsive)
            message response = client_->send_chat(convo_copy, &registry);
            
            if (response.tool_calls && !response.tool_calls->empty()) {
                convo_copy.push_back(response);

                for (const auto& call : *response.tool_calls) {
                    // Notify UI about the tool call
                    editor_event tool_ev;
                    tool_ev.type = event_type::agent_tool_update;
                    tool_ev.payload = call.function.name;
                    global_queue_.push(tool_ev);

                    std::string tool_result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
                    
                    message tool_msg;
                    tool_msg.role = "tool";
                    tool_msg.content = tool_result;
                    tool_msg.name = call.function.name;
                    tool_msg.tool_call_id = call.id;
                    
                    convo_copy.push_back(tool_msg);
                }
            } else {
                convo_copy.push_back(response);
                final_response = response.content;
                break;
            }
        }
        
        // Update the main thread's conversation history
        {
            std::lock_guard<std::mutex> lock(conversation_mutex_);
            conversation_ = convo_copy;
        }
        
        // Push result to UI queue
        editor_event ev;
        ev.type = event_type::agent_response;
        ev.payload = final_response;
        global_queue_.push(ev);

    }).detach();
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
    if (is_waiting_for_llm_) {
        attrset(COLOR_PAIR(15)); // Highlight
        mvaddstr(input_box_y + 2, input_box_x, " Waiting for LLM response... ");
    } else {
        attrset(COLOR_PAIR(10)); // Muted
        mvaddstr(input_box_y + 2, input_box_x, " Press ENTER to send. ");
    }
    attrset(0);
}
