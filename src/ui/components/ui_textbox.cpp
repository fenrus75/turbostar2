#include "ui/components/ui_textbox.h"
#include "input_history_manager.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "ansi.h"
#include "fs_utils.h"

// --- ui_textbox ---

ui_textbox::ui_textbox(std::string name, int x, int y, int width, const std::string &initial_text,
		       std::function<void(const std::string &)> on_submit, std::string label)
    : ui_element(std::move(name), x, y, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()),
      on_submit_(std::move(on_submit)), label_(std::move(label))
{
}

ui_textbox::ui_textbox(std::string name, int width, const std::string &initial_text, std::function<void(const std::string &)> on_submit,
		       std::string label)
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

	move(abs_y, input_x);
	for (int i = 0; i < static_cast<int>(display_text.length()); ++i) {
		int char_offset = display_offset + i;
		bool is_selected = false;
		if (selection_start_ != -1 && selection_end_ != -1) {
			int sel_min = std::min(selection_start_, selection_end_);
			int sel_max = std::max(selection_start_, selection_end_);
			if (char_offset >= sel_min && char_offset < sel_max) {
				is_selected = true;
			}
		}

		if (is_selected) {
			attrset(COLOR_PAIR(8));
		} else {
			attrset(COLOR_PAIR(5));
		}
		addch(display_text[i]);
	}
	attrset(COLOR_PAIR(5));
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
	std::string orig_buffer = buffer_;
	bool handled = false;

	if (ev.type == event_type::key_press) {
		if (has_focus_) {
			selection_start_ = -1;
			selection_end_ = -1;
		}
		if ((ev.key_code == '\n' || ev.key_code == '\r' || ev.key_code == KEY_ENTER) && has_focus_) {
			if (history_enabled_ && !buffer_.empty()) {
				input_history_manager::get_instance().add_entry(history_id_, buffer_);
				history_index_ = -1;
				traversal_edits_.clear();
			}
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
						handled = true;
						goto finish;
					}
				}
				if (cursor_pos_ < static_cast<int>(buffer_.length()))
					cursor_pos_++;
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_LEFT) {
				if (cursor_pos_ > 0)
					cursor_pos_--;
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_HOME || ev.key_code == 1) {
				cursor_pos_ = 0;
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_END || ev.key_code == 5) {
				cursor_pos_ = buffer_.length();
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (cursor_pos_ > 0) {
					buffer_.erase(cursor_pos_ - 1, 1);
					cursor_pos_--;
				}
				handled = true;
				goto finish;
			}
			if (ev.key_code == 25) { // Ctrl-Y: Clear textbox (delete line)
				buffer_.clear();
				cursor_pos_ = 0;
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_DC) { // Delete
				if (cursor_pos_ < static_cast<int>(buffer_.length())) {
					buffer_.erase(cursor_pos_, 1);
				}
				handled = true;
				goto finish;
			}
			if (ev.key_code >= 32 && ev.key_code <= 126) {
				buffer_.insert(cursor_pos_, 1, static_cast<char>(ev.key_code));
				cursor_pos_++;
				handled = true;
				goto finish;
			}

			if (ev.key_code == KEY_DOWN || ev.key_code == '\t') {
				if (history_enabled_ && ev.key_code == KEY_DOWN) {
					int max_index = input_history_manager::get_instance().get_history(history_id_).size();
					if (max_index > 0 && history_index_ > -1) {
						history_index_--;
						if (history_index_ == -1) {
							set_buffer(traversal_edits_[-1]);
						} else {
							set_buffer(traversal_edits_[history_index_]);
						}
					}
					handled = true;
					goto finish;
				}
				ui_element *p = parent_;
				while (p) {
					if (p->focus_next())
						break;
					p = p->parent();
				}
				handled = true;
				goto finish;
			}
			if (ev.key_code == KEY_UP || ev.key_code == KEY_BTAB) {
				if (history_enabled_ && ev.key_code == KEY_UP) {
					int max_index = input_history_manager::get_instance().get_history(history_id_).size();
					if (max_index > 0) {
						if (history_index_ == -1 && traversal_edits_.find(-1) == traversal_edits_.end()) {
							traversal_edits_[-1] = buffer_;
						}
						if (history_index_ < max_index - 1) {
							history_index_++;
							if (traversal_edits_.find(history_index_) != traversal_edits_.end()) {
								set_buffer(traversal_edits_[history_index_]);
							} else {
								const auto &hist = input_history_manager::get_instance().get_history(history_id_);
								std::string item = hist[hist.size() - 1 - history_index_];
								set_buffer(item);
								traversal_edits_[history_index_] = item;
							}
						}
					}
					handled = true;
					goto finish;
				}
				ui_element *p = parent_;
				while (p) {
					if (p->focus_previous())
						break;
					p = p->parent();
				}
				handled = true;
				goto finish;
			}
		}
	}

	if (ev.type == event_type::paste) {
		if (has_focus_) {
			selection_start_ = -1;
			selection_end_ = -1;
			std::string sanitized = ev.payload;
			// Strip newlines for a single-line textbox
			sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\n'), sanitized.end());
			sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), '\r'), sanitized.end());

			buffer_.insert(cursor_pos_, sanitized);
			cursor_pos_ += sanitized.length();
			handled = true;
			goto finish;
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
				selection_start_ = cursor_pos_;
				selection_end_ = cursor_pos_;
				is_mouse_selecting_ = true;
			}
			return true;
		}
	} else if (ev.type == event_type::mouse_drag) {
		if (is_mouse_selecting_) {
			int input_x = abs_x;
			int input_width = width_;
			if (!label_.empty()) {
				int offset = static_cast<int>(label_.length()) + 1;
				input_x += offset;
				input_width -= offset;
			}

			if (input_width > 0) {
				int drag_offset = ev.mouse_x - input_x;
				if (drag_offset < 0) {
					drag_offset = 0;
				}

				int display_offset = 0;
				if (cursor_pos_ >= input_width) {
					display_offset = cursor_pos_ - input_width + 1;
				}

				selection_end_ = std::min(static_cast<int>(buffer_.length()), drag_offset + display_offset);
				cursor_pos_ = selection_end_;
			}
			return true;
		}
	} else if (ev.type == event_type::mouse_release) {
		if (is_mouse_selecting_) {
			if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
				int sel_min = std::min(selection_start_, selection_end_);
				int sel_max = std::max(selection_start_, selection_end_);
				std::string selected_text = buffer_.substr(sel_min, sel_max - sel_min);
				if (!selected_text.empty()) {
					ansi::copy_to_clipboard(selected_text);
				}
			}
			is_mouse_selecting_ = false;
			return true;
		}
	}

	handled = false;

finish:
	if (buffer_ != orig_buffer && history_enabled_) {
		traversal_edits_[history_index_] = buffer_;
	}
	return handled;
}

std::optional<std::string> ui_textbox::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return buffer_;
	}
	return std::nullopt;
}

void ui_textbox::set_focus(bool focus)
{
	ui_element::set_focus(focus);
	if (!focus) {
		selection_start_ = -1;
		selection_end_ = -1;
		is_mouse_selecting_ = false;
		if (history_enabled_) {
			history_index_ = -1;
			traversal_edits_.clear();
		}
	}
}
