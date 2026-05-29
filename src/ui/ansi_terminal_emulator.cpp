#include "ansi_terminal_emulator.h"
#include <algorithm>

namespace ui {

ansi_terminal_emulator::ansi_terminal_emulator(int width, int height)
    : width_(width), height_(height) {
    resize(width, height);
}

void ansi_terminal_emulator::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    grid_.resize(height_);
    for (int y = 0; y < height_; ++y) {
        grid_[y].resize(width_, default_cell_);
    }

    cursor_x_ = std::min(cursor_x_, width_ - 1);
    cursor_y_ = std::min(cursor_y_, height_ - 1);
}

void ansi_terminal_emulator::clear_all() {
    for (int y = 0; y < height_; ++y) {
        grid_[y].assign(width_, default_cell_);
    }
    cursor_x_ = 0;
    cursor_y_ = 0;
}

void ansi_terminal_emulator::write(const std::string& bytes) {
    for (char c : bytes) {
        process_char(c);
    }
}

void ansi_terminal_emulator::process_char(char c) {
    if (state_ == parser_state::normal) {
        if (c == '\x1b') {
            state_ = parser_state::escape;
            esc_buffer_.clear();
        } else if (c == '\n') {
            cursor_y_++;
            if (cursor_y_ >= height_) {
                cursor_y_ = height_ - 1;
                scroll_up();
            }
        } else if (c == '\r') {
            cursor_x_ = 0;
        } else if (c == '\b') {
            if (cursor_x_ > 0) {
                cursor_x_--;
            }
        } else if (c == '\t') {
            cursor_x_ = (cursor_x_ + 8) & ~7;
            if (cursor_x_ >= width_) {
                cursor_x_ = 0;
                cursor_y_++;
                if (cursor_y_ >= height_) {
                    cursor_y_ = height_ - 1;
                    scroll_up();
                }
            }
        } else if (c >= 32) { // printable char
            if (cursor_x_ >= width_) {
                cursor_x_ = 0;
                cursor_y_++;
                if (cursor_y_ >= height_) {
                    cursor_y_ = height_ - 1;
                    scroll_up();
                }
            }
            if (cursor_y_ >= 0 && cursor_y_ < height_ && cursor_x_ >= 0 && cursor_x_ < width_) {
                grid_[cursor_y_][cursor_x_] = current_style_;
                grid_[cursor_y_][cursor_x_].glyph = c;
            }
            cursor_x_++;
        }
    } else if (state_ == parser_state::escape) {
        esc_buffer_ += c;
        if (c == '[') {
            state_ = parser_state::csi;
            csi_params_.clear();
            current_param_str_.clear();
        } else if (c == '(' || c == ')' || c == '*' || c == '+' || c == '#') {
            state_ = parser_state::escape_intermediate;
        } else {
            if (c == '7') { // save cursor
                saved_x_ = cursor_x_;
                saved_y_ = cursor_y_;
            } else if (c == '8') { // restore cursor
                cursor_x_ = saved_x_;
                cursor_y_ = saved_y_;
            }
            state_ = parser_state::normal;
        }
    } else if (state_ == parser_state::escape_intermediate) {
        esc_buffer_ += c;
        state_ = parser_state::normal;
    } else if (state_ == parser_state::csi) {
        if (c >= '0' && c <= '9') {
            current_param_str_ += c;
        } else if (c == ';') {
            if (!current_param_str_.empty()) {
                csi_params_.push_back(std::stoi(current_param_str_));
                current_param_str_.clear();
            } else {
                csi_params_.push_back(0);
            }
        } else if (c >= 0x40 && c <= 0x7E) { // final cmd char
            if (!current_param_str_.empty()) {
                csi_params_.push_back(std::stoi(current_param_str_));
                current_param_str_.clear();
            }
            handle_csi_command(c);
            state_ = parser_state::normal;
        }
    }
}

void ansi_terminal_emulator::handle_csi_command(char cmd) {
    auto get_param = [&](size_t idx, int def) -> int {
        if (idx < csi_params_.size()) {
            return csi_params_[idx];
        }
        return def;
    };

    switch (cmd) {
        case 'm': // SGR
            if (csi_params_.empty()) {
                current_style_ = default_cell_;
            } else {
                handle_sgr();
            }
            break;
        case 'H':
        case 'f': { // Cursor Position
            int row = get_param(0, 1) - 1;
            int col = get_param(1, 1) - 1;
            cursor_y_ = std::max(0, std::min(row, height_ - 1));
            cursor_x_ = std::max(0, std::min(col, width_ - 1));
            break;
        }
        case 'A': { // Cursor Up
            int n = get_param(0, 1);
            if (n == 0) n = 1;
            cursor_y_ = std::max(0, cursor_y_ - n);
            break;
        }
        case 'B': { // Cursor Down
            int n = get_param(0, 1);
            if (n == 0) n = 1;
            cursor_y_ = std::min(height_ - 1, cursor_y_ + n);
            break;
        }
        case 'C': { // Cursor Forward
            int n = get_param(0, 1);
            if (n == 0) n = 1;
            cursor_x_ = std::min(width_ - 1, cursor_x_ + n);
            break;
        }
        case 'D': { // Cursor Backward
            int n = get_param(0, 1);
            if (n == 0) n = 1;
            cursor_x_ = std::max(0, cursor_x_ - n);
            break;
        }
        case 'J': { // Erase in Display
            int mode = get_param(0, 0);
            if (mode == 2) {
                clear_all();
            } else if (mode == 0) {
                clear_line_part(cursor_y_, cursor_x_, width_ - 1);
                for (int y = cursor_y_ + 1; y < height_; ++y) {
                    clear_line_part(y, 0, width_ - 1);
                }
            } else if (mode == 1) {
                for (int y = 0; y < cursor_y_; ++y) {
                    clear_line_part(y, 0, width_ - 1);
                }
                clear_line_part(cursor_y_, 0, cursor_x_);
            }
            break;
        }
        case 'K': { // Erase in Line
            int mode = get_param(0, 0);
            if (mode == 0) {
                clear_line_part(cursor_y_, cursor_x_, width_ - 1);
            } else if (mode == 1) {
                clear_line_part(cursor_y_, 0, cursor_x_);
            } else if (mode == 2) {
                clear_line_part(cursor_y_, 0, width_ - 1);
            }
            break;
        }
        case 's': // Save cursor
            saved_x_ = cursor_x_;
            saved_y_ = cursor_y_;
            break;
        case 'u': // Restore cursor
            cursor_x_ = saved_x_;
            cursor_y_ = saved_y_;
            break;
        default:
            break;
    }
}

void ansi_terminal_emulator::handle_sgr() {
    for (int p : csi_params_) {
        if (p == 0) {
            current_style_ = default_cell_;
        } else if (p == 1) {
            current_style_.bold = true;
        } else if (p == 22) {
            current_style_.bold = false;
        } else if (p == 7) {
            current_style_.reverse = true;
        } else if (p == 27) {
            current_style_.reverse = false;
        } else if (p >= 30 && p <= 37) {
            current_style_.fg = p - 30;
        } else if (p == 39) {
            current_style_.fg = default_cell_.fg;
        } else if (p >= 40 && p <= 47) {
            current_style_.bg = p - 40;
        } else if (p == 49) {
            current_style_.bg = default_cell_.bg;
        } else if (p >= 90 && p <= 97) {
            current_style_.fg = 8 + (p - 90);
        } else if (p >= 100 && p <= 107) {
            current_style_.bg = 8 + (p - 100);
        }
    }
}

void ansi_terminal_emulator::scroll_up() {
    if (height_ <= 1) return;
    for (int y = 0; y < height_ - 1; ++y) {
        grid_[y] = std::move(grid_[y + 1]);
    }
    grid_[height_ - 1].assign(width_, default_cell_);
}

void ansi_terminal_emulator::clear_line_part(int y, int start_x, int end_x) {
    if (y < 0 || y >= height_) return;
    int s = std::max(0, start_x);
    int e = std::min(width_ - 1, end_x);
    for (int x = s; x <= e; ++x) {
        grid_[y][x] = default_cell_;
    }
}

} // namespace ui
