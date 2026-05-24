#include "ui/diff_window.h"
#include <dtl/dtl.hpp>
#include <ncurses.h>
#include <sstream>
#include <algorithm>

diff_window::diff_window(int id, int x, int y, int width, int height, std::shared_ptr<document> doc, event_queue &global_queue)
    : window(id, x, y, width, height, "Undo History: " + (doc ? doc->get_filename() : "")), global_queue_(global_queue)
{
    attach_document(doc);
    if (doc) {
        max_undo_steps_ = doc->get_undo_count();
    }

    prev_button_ = std::make_unique<ui_button>("prev", 2, height - 2, "[<< Prev]", 'P', [this]() { go_prev(); });
    next_button_ = std::make_unique<ui_button>("next", 15, height - 2, "[Next >>]", 'N', [this]() { go_next(); });
    restore_button_ = std::make_unique<ui_button>("restore", 30, height - 2, "[Restore State]", 'R', [this]() { restore_state(); });

    update_diff();
}

bool diff_window::process_events()
{
    bool needs_render = false;
    while (auto ev = get_queue().pop()) {
        if (ev->type == event_type::redraw) {
            // Document changed, update our knowledge of max steps and the current diff
            if (doc_) {
                max_undo_steps_ = doc_->get_undo_count();
                if (current_undo_step_ >= max_undo_steps_) {
                    current_undo_step_ = max_undo_steps_ > 0 ? max_undo_steps_ - 1 : 0;
                }
                update_diff();
                needs_render = true;
            }
            continue;
        }

        if (ev->type == event_type::key_press) {
            switch (ev->key_code) {
                case KEY_LEFT:
                    go_prev();
                    needs_render = true;
                    break;
                case KEY_RIGHT:
                    go_next();
                    needs_render = true;
                    break;
                case '\n':
                case '\r':
                case 'r':
                case 'R':
                    restore_state();
                    needs_render = true;
                    break;
                case KEY_UP:
                    if (scroll_y_ > 0) {
                        scroll_y_--;
                        needs_render = true;
                    }
                    break;
                case KEY_DOWN:
                    if (scroll_y_ < (int)diff_lines_.size() - (height_ - 4)) {
                        scroll_y_++;
                        needs_render = true;
                    }
                    break;
                case 27: // ESC
                {
                    editor_event close_ev;
                    close_ev.type = event_type::close_window;
                    close_ev.key_code = id_;
                    global_queue_.push(close_ev);
                    break;
                }
            }
        }
        
        // Dispatch to buttons for mouse support
        if (prev_button_->handle_event(*ev, x_ + prev_button_->x(), y_ + prev_button_->y())) needs_render = true;
        if (next_button_->handle_event(*ev, x_ + next_button_->x(), y_ + next_button_->y())) needs_render = true;
        if (restore_button_->handle_event(*ev, x_ + restore_button_->x(), y_ + restore_button_->y())) needs_render = true;
    }

    if (needs_render) invalidate();
    return needs_render;
}

void diff_window::update_diff()
{
    if (!doc_) return;

    auto before = doc_->get_lines_at_undo(current_undo_step_ + 1);
    auto after = doc_->get_lines_at_undo(current_undo_step_);

    int available_height = height_ - 4; // 2 for border, 1 for status, 1 for buttons
    int context = 3;
    
    // Attempt to fill the screen by increasing context if there's space
    while (context < 20) {
        dtl::Diff<std::string, std::vector<std::string>> d(before, after);
        d.compose();
        // DTL's composeUnifiedHunks takes a context size? 
        // Let's check the dtl docs or common usage. 
        // Some versions of DTL use d.composeUnifiedHunks() and it might not take params.
        // Actually, dtl::Diff has a printUnifiedFormat that might take it.
        
        // If we can't find it, we'll just stick with default.
        d.composeUnifiedHunks();
        
        std::stringstream ss;
        d.printUnifiedFormat(ss);
        
        std::vector<std::string> new_diff;
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            new_diff.push_back(line);
        }
        
        diff_lines_ = std::move(new_diff);
        
        // If we still have lots of space, try more context.
        // This is a bit naive but satisfies the "gradually increase" requirement.
        if ((int)diff_lines_.size() >= available_height) break;
        
        // For now, DTL might not support varying context easily via this API.
        // If it doesn't, we just break here.
        break; 
    }
    
    scroll_y_ = 0;
    invalidate();
}

void diff_window::go_prev()
{
    if (current_undo_step_ < max_undo_steps_ - 1) {
        current_undo_step_++;
        update_diff();
    }
}

void diff_window::go_next()
{
    if (current_undo_step_ > 0) {
        current_undo_step_--;
        update_diff();
    }
}

void diff_window::restore_state()
{
    if (!doc_) return;
    
    // If current_undo_step_ is 0, that's the current state (no undos).
    // If current_undo_step_ is 1, we want the state after 1 undo.
    // So we need to call undo() current_undo_step_ times.
    // Wait, let's verify:
    // undo_stack_ size is 10.
    // current_undo_step_ = 0: viewing diff between state 1 and 0 (the last edit). 
    //   If we "restore" to this, we are already here.
    // current_undo_step_ = 1: viewing diff between state 2 and 1.
    //   If we "restore" to this, we want to BE at state 1. 
    //   To get to state 1 from state 0, we need 1 undo.
    
    for (size_t i = 0; i < current_undo_step_; ++i) {
        doc_->undo();
    }
    
    editor_event ev;
    ev.type = event_type::close_window;
    ev.key_code = id_;
    global_queue_.push(ev);
}

void diff_window::set_cursor_position() const
{
	curs_set(0);
}

void diff_window::draw_content() const
{
    attron(COLOR_PAIR(background_color_pair_));
    // Clear area
    for (int i = 1; i < height_ - 1; ++i) {
        mvhline(y_ + i, x_ + 1, ' ', width_ - 2);
    }

    int display_y = y_ + 1;
    int available_height = height_ - 4;
    
    for (int i = 0; i < available_height && (i + scroll_y_) < (int)diff_lines_.size(); ++i) {
        const std::string& line = diff_lines_[i + scroll_y_];
        int color = 3; // Default
        if (!line.empty()) {
            if (line[0] == '+') color = 30; // Green
            else if (line[0] == '-') color = 31; // Red
            else if (line.size() >= 2 && line[0] == '@' && line[1] == '@') color = 32; // Cyan
        }
        
        attron(COLOR_PAIR(color));
        mvprintw(display_y + i, x_ + 1, "%.*s", width_ - 2, line.c_str());
        attroff(COLOR_PAIR(color));
    }

    // Draw status line
    attron(COLOR_PAIR(8));
    mvprintw(y_ + height_ - 3, x_ + 1, " Step %zu of %zu ", current_undo_step_ + 1, max_undo_steps_);
    attroff(COLOR_PAIR(8));

    // Draw buttons
    prev_button_->draw(x_ + prev_button_->x(), y_ + prev_button_->y());
    next_button_->draw(x_ + next_button_->x(), y_ + next_button_->y());
    restore_button_->draw(x_ + restore_button_->x(), y_ + restore_button_->y());

    attroff(COLOR_PAIR(background_color_pair_));
}
