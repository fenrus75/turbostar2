#include "ui/components/ui_textbox.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_textbox ---

ui_textbox::ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text,
		       std::function<void(const std::string &)> on_submit)
    : ui_element(std::move(name), x, y, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()),
      on_submit_(std::move(on_submit))
{
}

void ui_textbox::draw(int abs_x, int abs_y) const
{
	attrset(COLOR_PAIR(5));
	move(abs_y, abs_x);
	for (int i = 0; i < width_; ++i)
		addch(' ');

	std::string display_text = buffer_;
	int display_offset = 0;
	if (cursor_pos_ >= width_) {
		display_offset = cursor_pos_ - width_ + 1;
	}

	if (display_offset > 0 && display_offset < static_cast<int>(buffer_.length())) {
		display_text = buffer_.substr(display_offset);
	}

	std::string suggestion;
	if (has_focus_ && autocomplete_provider_ && !buffer_.empty()) {
		suggestion = autocomplete_provider_(buffer_);
	}

	std::string full_display = display_text;
	if (!suggestion.empty() && suggestion.length() > display_text.length()) {
		full_display = suggestion; // If suggestion is longer, we might draw it
	}

	if (static_cast<int>(full_display.length()) > width_) {
		full_display = full_display.substr(0, width_);
	}

	mvaddstr(abs_y, abs_x, display_text.c_str());
	if (!suggestion.empty() && suggestion.length() > buffer_.length()) {
		attrset(COLOR_PAIR(4)); // Fallback color for autocomplete
		int sug_x = abs_x + buffer_.length() - display_offset;
		if (sug_x < abs_x + width_) {
			std::string sug_draw = suggestion.substr(buffer_.length());
			if (sug_x + static_cast<int>(sug_draw.length()) > abs_x + width_) {
				sug_draw = sug_draw.substr(0, abs_x + width_ - sug_x);
			}
			mvaddstr(abs_y, sug_x, sug_draw.c_str());
		}
		attrset(COLOR_PAIR(5));
	}

	if (has_focus_) {
		int cursor_x = abs_x + cursor_pos_ - display_offset;
		if (cursor_x >= abs_x && cursor_x < abs_x + width_) {
			attron(COLOR_PAIR(14)); // Black text on green bg for cursor
			char c = ' ';
			if (cursor_pos_ < static_cast<int>(buffer_.length())) {
				c = buffer_[cursor_pos_];
			} else if (cursor_pos_ == static_cast<int>(buffer_.length()) && !suggestion.empty() &&
				   suggestion.length() > buffer_.length()) {
				c = suggestion[buffer_.length()];
			}
			mvaddch(abs_y, cursor_x, c);
			attroff(COLOR_PAIR(14));
		}
	}

	attroff(COLOR_PAIR(5));
}

bool ui_textbox::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		if ((ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			if (on_submit_)
				on_submit_(buffer_);
			return true;
		}

		if (has_focus_) {
			if (ev.key_code == KEY_RIGHT) {
				if (autocomplete_provider_ && !buffer_.empty()) {
					std::string sug = autocomplete_provider_(buffer_);
					if (!sug.empty() && sug.length() > buffer_.length() &&
					    cursor_pos_ == static_cast<int>(buffer_.length())) {
						set_buffer(sug);
						return true;
					}
				}
				if (cursor_pos_ < static_cast<int>(buffer_.length()))
					cursor_pos_++;
				return true;
			}
			if (ev.key_code == KEY_LEFT) {
				if (cursor_pos_ > 0)
					cursor_pos_--;
				return true;
			}
			if (ev.key_code == KEY_HOME) {
				cursor_pos_ = 0;
				return true;
			}
			if (ev.key_code == KEY_END) {
				cursor_pos_ = buffer_.length();
				return true;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (cursor_pos_ > 0) {
					buffer_.erase(cursor_pos_ - 1, 1);
					cursor_pos_--;
				}
				return true;
			}
			if (ev.key_code == 25) { // Ctrl-Y: Clear textbox (delete line)
				buffer_.clear();
				cursor_pos_ = 0;
				return true;
			}
			if (ev.key_code == KEY_DC) { // Delete
				if (cursor_pos_ < static_cast<int>(buffer_.length())) {
					buffer_.erase(cursor_pos_, 1);
				}
				return true;
			}
			if (ev.key_code >= 32 && ev.key_code <= 126) {
				buffer_.insert(cursor_pos_, 1, static_cast<char>(ev.key_code));
				cursor_pos_++;
				return true;
			}

			if (ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_next())
						break;
					p = p->parent();
				}
				return true;
			}
			if (ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_previous())
						break;
					p = p->parent();
				}
				return true;
			}
		}
	}

	if (ev.type == event_type::paste) {
		if (has_focus_) {
			std::string sanitized = ev.payload;
			// Strip newlines for a single-line textbox
			sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\n'), sanitized.end());
			sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\r'), sanitized.end());
			
			buffer_.insert(cursor_pos_, sanitized);
			cursor_pos_ += sanitized.length();
			return true;
		}
	}

	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			int click_offset = ev.mouse_x - abs_x;

			int display_offset = 0;
			if (cursor_pos_ >= width_) {
				display_offset = cursor_pos_ - width_ + 1;
			}

			cursor_pos_ = std::min(static_cast<int>(buffer_.length()), click_offset + display_offset);
			return true;
		}
	}

	return false;
}

std::optional<std::string> ui_textbox::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return buffer_;
	}
	return std::nullopt;
}
