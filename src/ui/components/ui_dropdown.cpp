#include "ui/components/ui_dropdown.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>

ui_dropdown::ui_dropdown(std::string name, int x, int y, int width, const std::string &initial_text,
			 const std::vector<std::string> &candidates, std::function<void(const std::string &)> on_change)
    : ui_element(std::move(name), x, y, width, 1), buffer_(initial_text), cursor_pos_(initial_text.length()), candidates_(candidates),
      on_change_(std::move(on_change))
{
	for (size_t i = 0; i < candidates_.size(); ++i) {
		if (candidates_[i] == buffer_) {
			selected_candidate_index_ = static_cast<int>(i);
			break;
		}
	}
}

void ui_dropdown::set_candidates(const std::vector<std::string> &candidates)
{
	candidates_ = candidates;
	selected_candidate_index_ = 0;
	for (size_t i = 0; i < candidates_.size(); ++i) {
		if (candidates_[i] == buffer_) {
			selected_candidate_index_ = static_cast<int>(i);
			break;
		}
	}
}

std::optional<std::string> ui_dropdown::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return buffer_;
	}
	return std::nullopt;
}

void ui_dropdown::draw(int abs_x, int abs_y) const
{
	attrset(COLOR_PAIR(5));
	move(abs_y, abs_x);
	for (int i = 0; i < width_; ++i)
		addch(' ');

	int max_text_width = width_ - 1;
	if (max_text_width < 0)
		max_text_width = 0;

	std::string display_text = buffer_;
	int display_offset = 0;
	if (cursor_pos_ >= max_text_width) {
		display_offset = cursor_pos_ - max_text_width + 1;
	}

	if (display_offset > 0 && display_offset < static_cast<int>(buffer_.length())) {
		display_text = buffer_.substr(display_offset);
	}

	if (static_cast<int>(display_text.length()) > max_text_width) {
		display_text = display_text.substr(0, max_text_width);
	}

	mvaddstr(abs_y, abs_x, display_text.c_str());

	if (width_ > 0) {
		if (has_focus_) {
			attrset(COLOR_PAIR(19));
		} else {
			attrset(COLOR_PAIR(17));
		}
		mvaddstr(abs_y, abs_x + width_ - 1, "▼");
		attrset(COLOR_PAIR(5));
	}

	if (has_focus_) {
		int cursor_x = abs_x + cursor_pos_ - display_offset;
		if (cursor_x >= abs_x && cursor_x < abs_x + max_text_width) {
			attron(COLOR_PAIR(14));
			char c = ' ';
			if (cursor_pos_ < static_cast<int>(buffer_.length())) {
				c = buffer_[cursor_pos_];
			}
			mvaddch(abs_y, cursor_x, c);
			attroff(COLOR_PAIR(14));
		}
	}

	attroff(COLOR_PAIR(5));
}

bool ui_dropdown::has_overlay() const
{
	return is_open_ && !candidates_.empty();
}

void ui_dropdown::draw_overlay(int abs_x, int abs_y) const
{
	if (!is_open_ || candidates_.empty()) {
		return;
	}

	int num_items = std::min(static_cast<int>(candidates_.size()), 5);
	int popup_w = width_;
	int popup_h = num_items + 2;

	int start_x = abs_x;
	int start_y = abs_y + 1;

	attrset(COLOR_PAIR(11));

	mvaddstr(start_y, start_x, "┌");
	for (int i = 1; i < popup_w - 1; ++i) {
		addstr("─");
	}
	addstr("┐");

	for (int i = 1; i < popup_h - 1; ++i) {
		mvaddstr(start_y + i, start_x, "│");
		attrset(COLOR_PAIR(1));
		for (int j = 1; j < popup_w - 1; ++j) {
			addch(' ');
		}
		attrset(COLOR_PAIR(11));
		mvaddstr(start_y + i, start_x + popup_w - 1, "│");
	}

	mvaddstr(start_y + popup_h - 1, start_x, "└");
	for (int i = 1; i < popup_w - 1; ++i) {
		addstr("─");
	}
	addstr("┘");

	for (int i = 0; i < num_items; ++i) {
		int item_idx = scroll_top_ + i;
		if (item_idx >= static_cast<int>(candidates_.size())) {
			break;
		}

		std::string item_text = candidates_[item_idx];
		int usable_width = popup_w - 2;
		if (usable_width < 0)
			usable_width = 0;

		if (static_cast<int>(item_text.length()) > usable_width) {
			item_text = item_text.substr(0, usable_width);
		} else {
			item_text.append(usable_width - item_text.length(), ' ');
		}

		if (item_idx == selected_candidate_index_) {
			attrset(COLOR_PAIR(19));
		} else {
			attrset(COLOR_PAIR(1));
		}

		mvaddstr(start_y + 1 + i, start_x + 1, item_text.c_str());
	}

	attrset(COLOR_PAIR(1));
}

bool ui_dropdown::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press) {
		int key = ev.key_code;

		if (is_open_) {
			int num_items = std::min(static_cast<int>(candidates_.size()), 5);

			if (key == KEY_UP) {
				if (selected_candidate_index_ > 0) {
					selected_candidate_index_--;
					if (selected_candidate_index_ < scroll_top_) {
						scroll_top_ = selected_candidate_index_;
					}
				}
				return true;
			}
			if (key == KEY_DOWN) {
				if (selected_candidate_index_ < static_cast<int>(candidates_.size()) - 1) {
					selected_candidate_index_++;
					if (selected_candidate_index_ >= scroll_top_ + num_items) {
						scroll_top_ = selected_candidate_index_ - num_items + 1;
					}
				}
				return true;
			}
			if (key == '\n' || key == '\r' || key == KEY_ENTER) {
				if (selected_candidate_index_ >= 0 && selected_candidate_index_ < static_cast<int>(candidates_.size())) {
					buffer_ = candidates_[selected_candidate_index_];
					cursor_pos_ = static_cast<int>(buffer_.length());
					if (on_change_) {
						on_change_(buffer_);
					}
				}
				is_open_ = false;
				return true;
			}
			if (key == 27) {
				is_open_ = false;
				return true;
			}
		} else {
			if (key == KEY_DOWN) {
				if (!candidates_.empty()) {
					is_open_ = true;
					selected_candidate_index_ = 0;
					for (size_t i = 0; i < candidates_.size(); ++i) {
						if (candidates_[i] == buffer_) {
							selected_candidate_index_ = static_cast<int>(i);
							break;
						}
					}
					int num_items = std::min(static_cast<int>(candidates_.size()), 5);
					if (selected_candidate_index_ < scroll_top_) {
						scroll_top_ = selected_candidate_index_;
					} else if (selected_candidate_index_ >= scroll_top_ + num_items) {
						scroll_top_ = selected_candidate_index_ - num_items + 1;
					}
				}
				return true;
			}
		}

		if (has_focus_) {
			if (key == KEY_RIGHT) {
				if (cursor_pos_ < static_cast<int>(buffer_.length()))
					cursor_pos_++;
				return true;
			}
			if (key == KEY_LEFT) {
				if (cursor_pos_ > 0)
					cursor_pos_--;
				return true;
			}
			if (key == KEY_HOME) {
				cursor_pos_ = 0;
				return true;
			}
			if (key == KEY_END) {
				cursor_pos_ = static_cast<int>(buffer_.length());
				return true;
			}
			if (key == KEY_BACKSPACE || key == 127 || key == 8) {
				if (cursor_pos_ > 0) {
					buffer_.erase(cursor_pos_ - 1, 1);
					cursor_pos_--;
					if (on_change_) {
						on_change_(buffer_);
					}
				}
				return true;
			}
			if (key == KEY_DC) {
				if (cursor_pos_ < static_cast<int>(buffer_.length())) {
					buffer_.erase(cursor_pos_, 1);
					if (on_change_) {
						on_change_(buffer_);
					}
				}
				return true;
			}
			if (key >= 32 && key <= 126) {
				buffer_.insert(cursor_pos_, 1, static_cast<char>(key));
				cursor_pos_++;
				if (on_change_) {
					on_change_(buffer_);
				}
				return true;
			}

			if (key == '\t' || key == KEY_BTAB || (key == KEY_UP && !is_open_)) {
				is_open_ = false;

				ui_element *p = parent_;
				while (p) {
					if (key == KEY_BTAB || key == KEY_UP) {
						if (p->focus_previous())
							break;
					} else {
						if (p->focus_next())
							break;
					}
					p = p->parent();
				}
				return true;
			}
		}
	}

	if (ev.type == event_type::mouse_click) {
		if (ev.mouse_x >= abs_x && ev.mouse_x < abs_x + width_ && ev.mouse_y == abs_y) {
			if (has_focus_) {
				if (!candidates_.empty()) {
					is_open_ = !is_open_;
					if (is_open_) {
						selected_candidate_index_ = 0;
						for (size_t i = 0; i < candidates_.size(); ++i) {
							if (candidates_[i] == buffer_) {
								selected_candidate_index_ = static_cast<int>(i);
								break;
							}
						}
					}
				}
			} else {
				ui_element *p = parent_;
				while (p) {
					p->set_focus(false);
					p = p->parent();
				}
				set_focus(true);
				if (!candidates_.empty()) {
					is_open_ = true;
					selected_candidate_index_ = 0;
					for (size_t i = 0; i < candidates_.size(); ++i) {
						if (candidates_[i] == buffer_) {
							selected_candidate_index_ = static_cast<int>(i);
							break;
						}
					}
				}
			}
			return true;
		}

		if (is_open_) {
			int num_items = std::min(static_cast<int>(candidates_.size()), 5);
			int popup_w = width_;
			int popup_h = num_items + 2;

			int start_x = abs_x;
			int start_y = abs_y + 1;

			if (ev.mouse_x >= start_x + 1 && ev.mouse_x < start_x + popup_w - 1 && ev.mouse_y >= start_y + 1 &&
			    ev.mouse_y < start_y + popup_h - 1) {
				int clicked_row = ev.mouse_y - (start_y + 1);
				int item_idx = scroll_top_ + clicked_row;
				if (item_idx >= 0 && item_idx < static_cast<int>(candidates_.size())) {
					selected_candidate_index_ = item_idx;
					buffer_ = candidates_[selected_candidate_index_];
					cursor_pos_ = static_cast<int>(buffer_.length());
					if (on_change_) {
						on_change_(buffer_);
					}
				}
				is_open_ = false;
				return true;
			} else {
				is_open_ = false;
			}
		}
	}

	return false;
}

bool ui_dropdown::contains_coordinate(int target_x, int target_y, int my_abs_x, int my_abs_y) const
{
	if (target_x >= my_abs_x && target_x < my_abs_x + width_ && target_y >= my_abs_y && target_y < my_abs_y + height_) {
		return true;
	}

	if (is_open_ && !candidates_.empty()) {
		int num_items = std::min(static_cast<int>(candidates_.size()), 5);
		int popup_w = width_;
		int popup_h = num_items + 2;
		int start_x = my_abs_x;
		int start_y = my_abs_y + 1;

		if (target_x >= start_x && target_x < start_x + popup_w && target_y >= start_y && target_y < start_y + popup_h) {
			return true;
		}
	}

	return false;
}
