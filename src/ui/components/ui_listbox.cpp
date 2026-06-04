#include "ui_listbox.h"
#include <ncurses.h>

ui_listbox::ui_listbox(std::string name, int x, int y, int width, int height, std::function<void(int)> on_selection_changed,
		       std::function<void(int)> on_submit)
    : ui_element(std::move(name), x, y, width, height), on_selection_changed_(std::move(on_selection_changed)),
      on_submit_(std::move(on_submit))
{
}

void ui_listbox::set_items(const std::vector<std::string> &items)
{
	items_ = items;
	if (selected_index_ >= (int)items_.size()) {
		selected_index_ = std::max(0, (int)items_.size() - 1);
	}
	// Adjust scroll if necessary
	if (selected_index_ < scroll_top_) {
		scroll_top_ = selected_index_;
	} else if (selected_index_ >= scroll_top_ + height_) {
		scroll_top_ = selected_index_ - height_ + 1;
	}
}

void ui_listbox::set_selected_index(int index)
{
	if (items_.empty()) {
		selected_index_ = 0;
		return;
	}
	selected_index_ = std::max(0, std::min(index, (int)items_.size() - 1));
	if (selected_index_ < scroll_top_) {
		scroll_top_ = selected_index_;
	} else if (selected_index_ >= scroll_top_ + height_) {
		scroll_top_ = selected_index_ - height_ + 1;
	}
}

void ui_listbox::draw(int abs_x, int abs_y) const
{
	int start_y = abs_y + y_;
	int start_x = abs_x + x_;

	for (int i = 0; i < height_; ++i) {
		int item_idx = scroll_top_ + i;
		if (item_idx < (int)items_.size()) {
			std::string display_text = items_[item_idx];
			if (display_text.length() < (size_t)width_) {
				display_text.append(width_ - display_text.length(), ' ');
			} else if (display_text.length() > (size_t)width_) {
				display_text = display_text.substr(0, width_);
			}

			if (item_idx == selected_index_ && has_focus_) {
				attron(COLOR_PAIR(8));
				mvprintw(start_y + i, start_x, "%s", display_text.c_str());
				attroff(COLOR_PAIR(8));
			} else {
				attron(COLOR_PAIR(12));
				mvprintw(start_y + i, start_x, "%s", display_text.c_str());
				attroff(COLOR_PAIR(12));
			}
		} else {
			attron(COLOR_PAIR(12));
			// Empty space
			std::string empty(width_, ' ');
			mvprintw(start_y + i, start_x, "%s", empty.c_str());
			attroff(COLOR_PAIR(12));
		}
	}
}

bool ui_listbox::handle_event(const editor_event &ev, int /*abs_x*/, int /*abs_y*/)
{
	if (ev.type == event_type::key_press) {
		int key = ev.key_code;
		if (key == KEY_UP) {
			if (selected_index_ > 0) {
				set_selected_index(selected_index_ - 1);
				if (on_selection_changed_) {
					on_selection_changed_(selected_index_);
				}
				return true;
			}
		} else if (key == KEY_DOWN) {
			if (selected_index_ < (int)items_.size() - 1) {
				set_selected_index(selected_index_ + 1);
				if (on_selection_changed_) {
					on_selection_changed_(selected_index_);
				}
				return true;
			}
		} else if (key == ' ') {
			if (on_space_ && !items_.empty() && selected_index_ >= 0 && selected_index_ < (int)items_.size()) {
				on_space_(selected_index_);
				return true;
			}
		} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			if (on_submit_ && !items_.empty() && selected_index_ >= 0 && selected_index_ < (int)items_.size()) {
				on_submit_(selected_index_);
			}
			return true;
		}
	}
	return false;
}

std::optional<std::string> ui_listbox::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return std::to_string(selected_index_);
	}
	return std::nullopt;
}