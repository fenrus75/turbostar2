#include "menu_bar.h"
#include "event_logger.h"
#include <ncurses.h>
#include <cctype>

menu_bar::menu_bar()
{
	categories_ = {
		{"File", 'f', {{"Exit", event_type::quit}}},
		{"Edit", 'e', {}},
		{"Search", 's', {}},
		{"Help", 'h', {}}
	};
}

bool menu_bar::handle_alt_key(char c, event_queue& queue)
{
	(void)queue;
	c = std::tolower(c);
	for (size_t i = 0; i < categories_.size(); ++i) {
		if (categories_[i].hotkey == c) {
			active_category_ = static_cast<int>(i);
			selected_item_ = 0;
			event_logger::get_instance().log("Menu activated: " + categories_[i].name);
			return true;
		}
	}
	return false;
}

bool menu_bar::is_open() const
{
	return active_category_ != -1;
}

void menu_bar::close_menu()
{
	active_category_ = -1;
}

bool menu_bar::handle_key(int key, event_queue& queue)
{
	if (!is_open()) return false;
	
	if (key == 27) { // ESC
		close_menu();
		return true;
	}
	
	if (key == KEY_DOWN) {
		if (!categories_[active_category_].items.empty()) {
			selected_item_ = (selected_item_ + 1) % categories_[active_category_].items.size();
		}
		return true;
	} else if (key == KEY_UP) {
		if (!categories_[active_category_].items.empty()) {
			selected_item_ = (selected_item_ - 1 + categories_[active_category_].items.size()) % categories_[active_category_].items.size();
		}
		return true;
	} else if (key == KEY_RIGHT) {
		active_category_ = (active_category_ + 1) % categories_.size();
		selected_item_ = 0;
		event_logger::get_instance().log("Menu activated: " + categories_[active_category_].name);
		return true;
	} else if (key == KEY_LEFT) {
		active_category_ = (active_category_ - 1 + categories_.size()) % categories_.size();
		selected_item_ = 0;
		event_logger::get_instance().log("Menu activated: " + categories_[active_category_].name);
		return true;
	} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
		if (!categories_[active_category_].items.empty()) {
			editor_event ev;
			ev.type = categories_[active_category_].items[selected_item_].action;
			queue.push(ev);
		}
		close_menu();
		return true;
	}
	
	return true; // Consume other keys while menu is open
}

void menu_bar::draw() const
{
	move(0, 0);
	attron(COLOR_PAIR(1));
	for (int i = 0; i < COLS; ++i) { addch(' '); }
	
	int col = 1;
	std::vector<int> category_cols;
	
	for (size_t i = 0; i < categories_.size(); ++i) {
		category_cols.push_back(col);
		move(0, col);
		
		bool is_active = (active_category_ == static_cast<int>(i));
		
		if (is_active) {
			attron(A_REVERSE);
		}
		
		attron(COLOR_PAIR(2));
		addch(categories_[i].name[0]);
		attroff(COLOR_PAIR(2));
		
		attron(COLOR_PAIR(1));
		printw("%s", categories_[i].name.c_str() + 1);
		
		if (is_active) {
			attroff(A_REVERSE);
		}
		
		col += 2 + categories_[i].name.length();
	}
	attroff(COLOR_PAIR(1));
	
	if (active_category_ != -1) {
		const auto& cat = categories_[active_category_];
		int drop_col = category_cols[active_category_];
		int drop_width = 16;
		int drop_height = cat.items.size() + 2;
		
		if (drop_height > 2) {
			attron(COLOR_PAIR(1));
			mvaddstr(1, drop_col, "┌");
			for(int j=1; j<drop_width-1; ++j) addstr("─");
			addstr("┐");
			
			for (size_t i = 0; i < cat.items.size(); ++i) {
				mvaddstr(2 + i, drop_col, "│");
				
				if (static_cast<int>(i) == selected_item_) {
					attron(A_REVERSE);
				}
				printw(" %-*s ", drop_width - 4, cat.items[i].name.c_str());
				if (static_cast<int>(i) == selected_item_) {
					attroff(A_REVERSE);
				}
				
				mvaddstr(2 + i, drop_col + drop_width - 1, "│");
			}
			
			mvaddstr(2 + cat.items.size(), drop_col, "└");
			for(int j=1; j<drop_width-1; ++j) addstr("─");
			addstr("┘");
			attroff(COLOR_PAIR(1));
		} else {
			// Empty menu drop down indicator
			attron(COLOR_PAIR(1));
			mvaddstr(1, drop_col, "┌");
			for(int j=1; j<drop_width-1; ++j) addstr("─");
			addstr("┐");
			mvaddstr(2, drop_col, "│");
			printw(" %-*s ", drop_width - 4, "(empty)");
			mvaddstr(2, drop_col + drop_width - 1, "│");
			mvaddstr(3, drop_col, "└");
			for(int j=1; j<drop_width-1; ++j) addstr("─");
			addstr("┘");
			attroff(COLOR_PAIR(1));
		}
	}
}
