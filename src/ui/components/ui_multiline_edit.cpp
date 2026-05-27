#include "ui_multiline_edit.h"
#include <ncurses.h>

ui_multiline_edit::ui_multiline_edit(std::string name, int x, int y, int width, int height,
				     std::function<void(const std::string &)> on_submit)
    : ui_element(std::move(name), x, y, width, height), on_submit_(std::move(on_submit))
{
}

void ui_multiline_edit::set_buffer(const std::string &text)
{
	buffer_ = text;
	cursor_pos_ = buffer_.length();
	update_scroll();
}

void ui_multiline_edit::update_scroll()
{
	// Basic logic to keep the cursor visible.
	// Since we wrap lines, we calculate how many screen lines the text takes.
	int line_count = 0;
	int current_line_len = 2; // account for "> " prefix
	int cursor_screen_y = 0;

	for (int i = 0; i <= (int)buffer_.length(); ++i) {
		if (i == cursor_pos_) {
			cursor_screen_y = line_count;
		}

		if (i < (int)buffer_.length()) {
			if (buffer_[i] == '\n' || current_line_len >= width_) {
				line_count++;
				current_line_len = 0;
			} else {
				current_line_len++;
			}
		}
	}

	if (cursor_screen_y < scroll_offset_) {
		scroll_offset_ = cursor_screen_y;
	} else if (cursor_screen_y >= scroll_offset_ + height_) {
		scroll_offset_ = cursor_screen_y - height_ + 1;
	}
}

void ui_multiline_edit::draw(int abs_x, int abs_y) const
{
	int start_y = abs_y + y_;
	int start_x = abs_x + x_;

	attrset(COLOR_PAIR(1)); // Normal text color (Black on Light Gray) or Window background
	// We'll let the parent set the default color, but for now use COLOR_PAIR(1)

	// Clear area
	for (int i = 0; i < height_; ++i) {
		move(start_y + i, start_x);
		for (int j = 0; j < width_; ++j)
			addch(' ');
	}

	std::string text_to_draw = "> " + buffer_;
	int physical_cursor_pos = cursor_pos_ + 2;

	int draw_y = 0;
	int draw_x = 0;

	int c_y = -1;
	int c_x = -1;

	for (size_t i = 0; i <= text_to_draw.length(); ++i) {
		if ((int)i == physical_cursor_pos) {
			c_y = draw_y;
			c_x = draw_x;
		}

		if (i < text_to_draw.length()) {
			if (text_to_draw[i] == '\n' || draw_x >= width_) {
				draw_y++;
				draw_x = 0;
			}
			if (text_to_draw[i] != '\n' && draw_y >= scroll_offset_ && draw_y < scroll_offset_ + height_) {
				mvaddch(start_y + draw_y - scroll_offset_, start_x + draw_x, text_to_draw[i]);
			}
			if (text_to_draw[i] != '\n') {
				draw_x++;
			}
		}
	}

	if (has_focus_ && c_y >= scroll_offset_ && c_y < scroll_offset_ + height_) {
		move(start_y + c_y - scroll_offset_, start_x + c_x);
		// Leave physical terminal cursor here
	}
}

bool ui_multiline_edit::handle_event(const editor_event &ev, int /*abs_x*/, int /*abs_y*/)
{
	std::string orig_buffer = buffer_;
	bool handled = false;

	if (ev.type == event_type::key_press) {
		int key = ev.key_code;

		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (cursor_pos_ > 0) {
				buffer_.erase(cursor_pos_ - 1, 1);
				cursor_pos_--;
				update_scroll();
				handled = true;
			}
		} else if (key == KEY_DC) { // Delete
			if (cursor_pos_ < (int)buffer_.length()) {
				buffer_.erase(cursor_pos_, 1);
				update_scroll();
				handled = true;
			}
		} else if (key == KEY_LEFT) {
			if (cursor_pos_ > 0) {
				cursor_pos_--;
				update_scroll();
				handled = true;
			}
		} else if (key == KEY_RIGHT) {
			if (cursor_pos_ < (int)buffer_.length()) {
				cursor_pos_++;
				update_scroll();
				handled = true;
			}
		} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			if (on_submit_ && !buffer_.empty()) {
				on_submit_(buffer_);
				buffer_.clear();
				cursor_pos_ = 0;
				update_scroll();
			}
			handled = true;
		} else if (key >= 32 && key <= 126) {
			buffer_.insert(cursor_pos_, 1, static_cast<char>(key));
			cursor_pos_++;
			update_scroll();
			handled = true;
		}
	} else if (ev.type == event_type::paste) {
		if (has_focus_) {
			buffer_.insert(cursor_pos_, ev.payload);
			cursor_pos_ += ev.payload.length();
			update_scroll();
			handled = true;
		}
	}
	
	if (handled && buffer_ != orig_buffer && on_change_) {
		on_change_(buffer_);
	}
	return handled;
}