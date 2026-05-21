#include "ui/popup_menu.h"
#include <ncurses.h>
#include <cctype>

popup_menu::popup_menu(int x, int y, const std::vector<popup_menu_item>& items)
    : x_(x), y_(y), items_(items)
{
	width_ = 15; // Minimum width
	for (const auto& item : items_) {
		int w = item.name.length() + 4; // Margin + name
		if (item.hotkey != 0) w += 2; // For potential display spacing
		if (w > width_) width_ = w;
	}
	height_ = items_.size() + 2; // +2 for top/bottom borders

	// Ensure selected_idx_ points to a valid, non-separator item
	while (selected_idx_ < static_cast<int>(items_.size()) && items_[selected_idx_].is_separator) {
		selected_idx_++;
	}

	// Clamp to screen edges (account for shadow width + 1)
	if (x_ + width_ + 1 > COLS) {
		x_ = COLS - width_ - 1;
	}
	if (x_ < 0) {
		x_ = 0;
	}
}

void popup_menu::draw() const
{
	// Draw shadow
	attron(COLOR_PAIR(6));
	for (int i = 0; i < height_; ++i)
		mvaddch(y_ + i, x_ + width_, ' ');
	for (int i = 0; i < width_; ++i)
		mvaddch(y_ + height_, x_ + 1 + i, ' ');
	attroff(COLOR_PAIR(6));

	attron(COLOR_PAIR(1));
	mvaddstr(y_, x_, "┌");
	for (int j = 1; j < width_ - 1; ++j)
		addstr("─");
	addstr("┐");

	for (size_t i = 0; i < items_.size(); ++i) {
		const auto& item = items_[i];
		int row_y = y_ + 1 + i;
		if (item.is_separator) {
			mvaddstr(row_y, x_, "├");
			for (int j = 1; j < width_ - 1; ++j)
				addstr("─");
			addstr("┤");
		} else {
			bool selected = (static_cast<int>(i) == selected_idx_);
			mvaddstr(row_y, x_, "│");
			
			if (selected)
				attrset(COLOR_PAIR(14));
			else
				attrset(COLOR_PAIR(1));

			// Fill background
			for (int j = 1; j < width_ - 1; ++j)
				mvaddch(row_y, x_ + j, ' ');
			
			move(row_y, x_ + 1);
			
			// Find hotkey
			size_t hotkey_pos = std::string::npos;
			if (item.hotkey != 0) {
				std::string lower_name = item.name;
				for (char& c : lower_name) c = std::tolower(c);
				hotkey_pos = lower_name.find(std::tolower(item.hotkey));
			}

			// Draw name
			for (size_t j = 0; j < item.name.length(); ++j) {
				if (j == hotkey_pos) {
					if (selected) attron(COLOR_PAIR(15));
					else attron(COLOR_PAIR(2));
					
					addch(item.name[j]);
					
					if (selected) attron(COLOR_PAIR(14));
					else attron(COLOR_PAIR(1));
				} else {
					addch(item.name[j]);
				}
			}

			attrset(COLOR_PAIR(1));
			mvaddstr(row_y, x_ + width_ - 1, "│");
		}
	}

	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + height_ - 1, x_, "└");
	for (int j = 1; j < width_ - 1; ++j)
		addstr("─");
	addstr("┘");
	attrset(0);
}

std::optional<int> popup_menu::handle_key(int key)
{
	if (key == 27) { // ESC
		return cancel_id;
	} else if (key == KEY_UP) {
		do {
			selected_idx_ = (selected_idx_ - 1 + items_.size()) % items_.size();
		} while (items_[selected_idx_].is_separator);
		return std::nullopt;
	} else if (key == KEY_DOWN) {
		do {
			selected_idx_ = (selected_idx_ + 1) % items_.size();
		} while (items_[selected_idx_].is_separator);
		return std::nullopt;
	} else if (key == '\n' || key == 13 || key == KEY_ENTER) {
		if (selected_idx_ >= 0 && selected_idx_ < static_cast<int>(items_.size()) && !items_[selected_idx_].is_separator) {
			return items_[selected_idx_].id;
		}
	} else {
		// Hotkeys
		char c = std::tolower(static_cast<char>(key));
		for (const auto& item : items_) {
			if (!item.is_separator && item.hotkey != 0 && std::tolower(item.hotkey) == c) {
				return item.id;
			}
		}
	}
	return std::nullopt;
}

std::optional<int> popup_menu::handle_mouse(int mouse_x, int mouse_y)
{
	// Check if click is within the menu body (excluding border shadows)
	if (mouse_x >= x_ && mouse_x < x_ + width_ && mouse_y >= y_ && mouse_y < y_ + height_) {
		int item_idx = mouse_y - y_ - 1;
		if (item_idx >= 0 && item_idx < static_cast<int>(items_.size())) {
			if (!items_[item_idx].is_separator) {
				selected_idx_ = item_idx; // update highlight just in case it takes a moment to close
				return items_[item_idx].id;
			}
		}
		// Clicked inside but on a border or separator, do nothing but consume event
		return std::nullopt;
	}
	// Clicked outside the menu
	return cancel_id;
}