#include "ui/components/ui_textbox.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_textbox ---

ui_textbox::ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text,
		       std::function<void(const std::string &)> on_submit, std::string label)
    : ui_element(std::move(name), x, y, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()),
      on_submit_(std::move(on_submit)), label_(std::move(label))
{
}

ui_textbox::ui_textbox(std::string name, int width, const std::string &initial_text,
		       std::function<void(const std::string &)> on_submit, std::string label)
    : ui_element(std::move(name), 0, 0, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()),
      on_submit_(std::move(on_submit)), label_(std::move(label))
{
}

void ui_textbox::draw(int abs_x, int abs_y) const
{
	int input_x = abs_x;
	int input_width = width_;

	if (!label_.empty()) {
		attrset(COLOR_PAIR(1));
		mvaddstr(abs_y, abs_x, label_.c_str());
		int offset = static_cast<int>(label_.length()) + 1;
		input_x += offset;
		input_width -= offset;
	}

	if (input_width <= 0) {
		return;
	}

	attrset(COLOR_PAIR(5));
	move(abs_y, input_x);
	for (int i = 0; i < input_width; ++i)
		addch(' ');

	std::string display_text = buffer_;
	int display_offset = 0;
	if (cursor_pos_ >= input_width) {
		display_offset = cursor_pos_ - input_width + 1;
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
		full_display = suggestion;
	}

	if (static_cast<int>(full_display.length()) > input_width) {
		full_display = full_display.substr(0, input_width);
	}

	mvaddstr(abs_y, input_x, display_text.c_str());
	if (!suggestion.empty() && suggestion.length() > buffer_.length()) {
		attrset(COLOR_PAIR(4));
		int sug_x = input_x + buffer_.length() - display_offset;
		if (sug_x < input_x + input_width) {
			std::string sug_draw = suggestion.substr(buffer_.length());
			if (sug_x + static_cast<int>(sug_draw.length()) > input_x + input_width) {
				sug_draw = sug_draw.substr(0, input_x + input_width - sug_x);
			}
			mvaddstr(abs_y, sug_x, sug_draw.c_str());
		}
		attrset(COLOR_PAIR(5));
	}

	if (has_focus_) {
		int cursor_x = input_x + cursor_pos_ - display_offset;
		if (cursor_x >= input_x && cursor_x < input_x + input_width) {
			attron(COLOR_PAIR(14));
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
			int input_x = abs_x;
			int input_width = width_;
			if (!label_.empty()) {
				int offset = static_cast<int>(label_.length()) + 1;
				input_x += offset;
				input_width -= offset;
			}

			if (input_width > 0) {
				int click_offset = ev.mouse_x - input_x;
				if (click_offset < 0) {
					click_offset = 0;
				}

				int display_offset = 0;
				if (cursor_pos_ >= input_width) {
					display_offset = cursor_pos_ - input_width + 1;
				}

				cursor_pos_ = std::min(static_cast<int>(buffer_.length()), click_offset + display_offset);
			}
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
