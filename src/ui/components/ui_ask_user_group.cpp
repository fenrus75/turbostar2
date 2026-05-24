#include "ui/components/ui_ask_user_group.h"
#include <ncurses.h>
#include <algorithm>
#include <cctype>
#include "ui/dialog.h"

namespace tools {

ui_ask_user_group::ui_ask_user_group(std::string name, int x, int y, int width, const std::vector<std::string>& options)
    : ui_element(std::move(name), x, y, width, 0), options_(options) {
    layout_items();
}

void ui_ask_user_group::layout_items() {
    items_.clear();
    int current_y = 0; // Start at 0, box top will draw at y=-1 cleanly.
    int wrap_w = width_ - 8;

    for (const auto& opt : options_) {
        item_info info;
        wrap_text(opt, wrap_w, info.lines);
        info.height = info.lines.size();
        info.y_offset = current_y;
        items_.push_back(info);
        current_y += info.height + 1; // 1 line separation (shared by box bottom/top)
    }

    // Add "Other" item
    item_info other;
    other.lines.push_back(other_text_.empty() ? "Other: " : "Other: " + other_text_);
    other.height = 1;
    other.y_offset = current_y;
    items_.push_back(other);
    
    height_ = current_y + other.height + 1; // +1 for the last item's box bottom
}

void ui_ask_user_group::wrap_text(const std::string& text, int width, std::vector<std::string>& out_lines) {
    if (text.empty()) {
        out_lines.push_back("");
        return;
    }
    
    std::string current_line;
    std::string word;
    for (char c : text) {
        if (isspace(c)) {
            if (!word.empty()) {
                if (!current_line.empty() && current_line.length() + 1 + word.length() > (size_t)width) {
                    out_lines.push_back(current_line);
                    current_line = word;
                } else {
                    if (!current_line.empty()) current_line += " ";
                    current_line += word;
                }
                word.clear();
            }
            if (c == '\n') {
                out_lines.push_back(current_line);
                current_line.clear();
            }
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        if (!current_line.empty() && current_line.length() + 1 + word.length() > (size_t)width) {
            out_lines.push_back(current_line);
            current_line = word;
        } else {
            if (!current_line.empty()) current_line += " ";
            current_line += word;
        }
    }
    if (!current_line.empty()) out_lines.push_back(current_line);
}

void ui_ask_user_group::draw(int abs_x, int abs_y) const {
    int start_x = abs_x;
    int start_y = abs_y;

    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& info = items_[i];
        bool selected = (selected_index_ == (int)i);
        
        int pair = selected ? 5 : 17; // 5: White on Dark Blue, 17: Black on Cyan
        attrset(COLOR_PAIR(pair));

        // Draw the block background (full width of the group minus side margins)
        for (int h = 0; h < info.height; ++h) {
            move(start_y + info.y_offset + h, start_x + 2);
            for (int w = 0; w < width_ - 4; ++w) addch(' ');
            
            // Draw text
            if (h < (int)info.lines.size()) {
                std::string line = info.lines[h];
                if (i == items_.size() - 1) {
                     line = "Other: " + other_text_;
                }
                
                // Centering line within the background block
                int line_x = (width_ - 4 - line.length()) / 2;
                mvaddstr(start_y + info.y_offset + h, start_x + 2 + line_x, line.c_str());
                
                // Draw cursor if editing other and selected
                if (selected && i == items_.size() - 1 && has_focus_) {
                    attron(A_REVERSE);
                    mvaddch(start_y + info.y_offset + h, start_x + 2 + line_x + 7 + cursor_pos_, 
                            cursor_pos_ < (int)other_text_.length() ? other_text_[cursor_pos_] : ' ');
                    attroff(A_REVERSE);
                    attrset(COLOR_PAIR(pair));
                }
            }
        }

        // Draw box if selected
        if (selected) {
            attrset(COLOR_PAIR(11)); // High contrast border
            // Top
            mvaddstr(start_y + info.y_offset - 1, start_x + 1, "┌");
            for (int w = 0; w < width_ - 4; ++w) addstr("─");
            addstr("┐");
            
            // Sides
            for (int h = 0; h < info.height; ++h) {
                mvaddstr(start_y + info.y_offset + h, start_x + 1, "│");
                mvaddstr(start_y + info.y_offset + h, start_x + width_ - 2, "│");
            }
            
            // Bottom
            mvaddstr(start_y + info.y_offset + info.height, start_x + 1, "└");
            for (int w = 0; w < width_ - 4; ++w) addstr("─");
            addstr("┘");
        }
    }
}

bool ui_ask_user_group::handle_event(const editor_event& ev, int /*abs_x*/, int /*abs_y*/) {
    if (ev.type == event_type::key_press) {
        int key = ev.key_code;
        
        if (key == KEY_UP) {
            if (selected_index_ > 0) {
                selected_index_--;
                cursor_pos_ = other_text_.length();
                return true;
            }
        } else if (key == KEY_DOWN) {
            if (selected_index_ < (int)items_.size() - 1) {
                selected_index_++;
                cursor_pos_ = other_text_.length();
                return true;
            }
        } else if (key == '\t') {
            // Tab to go to other box directly? Or just focus next?
            // The user said "Tab as shortcut to go to the edit box is a good idea."
            if (selected_index_ != (int)items_.size() - 1) {
                selected_index_ = items_.size() - 1;
                cursor_pos_ = other_text_.length();
                return true;
            }
            // If already on other, let parent handle it (to go to OK/Cancel)
        } else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            if (parent_) {
                dialog* d = dynamic_cast<dialog*>(parent_);
                if (d) {
                    if (selected_index_ == (int)items_.size() - 1) {
                        d->set_result(other_text_);
                    } else {
                        d->set_result(options_[selected_index_]);
                    }
                    d->set_action(dialog_result::confirmed);
                }
            }
            return true;
        } else if (selected_index_ == (int)items_.size() - 1) {
            // Typing in "Other"
            if (key == KEY_BACKSPACE || key == 127 || key == '\b') {
                if (cursor_pos_ > 0) {
                    other_text_.erase(cursor_pos_ - 1, 1);
                    cursor_pos_--;
                }
                return true;
            } else if (key == KEY_DC) { // Delete
                if (cursor_pos_ < (int)other_text_.length()) {
                    other_text_.erase(cursor_pos_, 1);
                }
                return true;
            } else if (key == KEY_LEFT) {
                if (cursor_pos_ > 0) cursor_pos_--;
                return true;
            } else if (key == KEY_RIGHT) {
                if (cursor_pos_ < (int)other_text_.length()) cursor_pos_++;
                return true;
            } else if (isprint(key)) {
                other_text_.insert(cursor_pos_, 1, (char)key);
                cursor_pos_++;
                return true;
            }
        }
    }
    return false;
}

std::optional<std::string> ui_ask_user_group::get_value(const std::string& target_name) const {
    if (target_name == name_) {
        if (selected_index_ == (int)items_.size() - 1) return other_text_;
        return options_[selected_index_];
    }
    return std::nullopt;
}

} // namespace tools
