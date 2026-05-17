#include "menu_bar.h"
#include <cctype>
#include <ncurses.h>
#include "event_logger.h"

menu_bar::menu_bar()
{
	categories_ = {{"File",
			'f',
			{{"New", event_type::new_doc, 'n', "^KN", false},
			 {"Open...", event_type::load, 'o', "^KE", false},
			 {"Save", event_type::save, 's', "^KS", false},
			 {"Save as...", event_type::save_as, 'a', "^KW", false},
			 {"Save All", event_type::save_all, 'v', "^KA", false},
			 {"", event_type::key_press, 0, "", true},
			 menu_item("Exit", event_type::quit, 'x', "^KQ", false)}},
		       {"Edit",
			'e',
			{
			    {"Delete Line", event_type::key_press, 25, 'y', "^Y", false},
			    {"Delete to EOL", event_type::key_press, 10, 'j', "^J", false},
			    {"Delete Word Forward", event_type::key_press, 23, 'w', "^W", false},
			    {"Delete Word Backward", event_type::key_press, 15, 'o', "^O", false},
			    {"", event_type::key_press, 0, "", true},
			    {"Format Document", event_type::format_doc, 'f', "^KJ", false},
			}},
		       {"Search",
			's',
			{{"Find...", event_type::find, 'f', "^KF", false},
			 {"Replace...", event_type::replace, 'r', "^QA", false},
			 {"Find next", event_type::key_press, 12, 'l', "^L", false}}},
		       {"Tools",
			't',
			{{"Compile", event_type::compile, 'c', "F9", false},
			 {"Compile File", event_type::compile_file, 'p', "^KP", false},
			 {"Run Tests", event_type::run_tests, 't', "F8", false}}},
		       {"Options",
			'p',
			{{"Preferences...", event_type::settings, 'p', "", false}}},
		       {"Git",
			'g',
			{{"Git add", event_type::git_add, 'a', "", false},
			 {"Git refresh", event_type::git_refresh, 'r', "", false}}},
		       {"Window", 'w', {}},
		       {"Help", 'h', {{"About...", event_type::about, 'a', "", false}}}};
}

bool menu_bar::handle_alt_key(char c, event_queue &queue)
{
	(void)queue;
	c = std::tolower(c);
	for (size_t i = 0; i < categories_.size(); ++i) {
		if (categories_[i].hotkey == c) {
			active_category_ = static_cast<int>(i);
			selected_item_ = 0;
			if (!categories_[active_category_].items.empty() && categories_[active_category_].items[0].is_separator) {
				find_next_item();
			}
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

void menu_bar::set_category_items(const std::string &name, const std::vector<menu_item> &items)
{
	for (auto &cat : categories_) {
		if (cat.name == name) {
			cat.items = items;
			return;
		}
	}
}

bool menu_bar::handle_key(int key, event_queue &queue)
{
	if (!is_open())
		return false;
	event_logger::get_instance().log("Menu handle_key: " + std::to_string(key));

	if (key == 27) { // ESC
		close_menu();
		return true;
	}

	if (key == KEY_DOWN) {
		find_next_item();
		return true;
	} else if (key == KEY_UP) {
		find_prev_item();
		return true;
	} else if (key == KEY_RIGHT) {
		active_category_ = (active_category_ + 1) % categories_.size();
		selected_item_ = 0;
		if (!categories_[active_category_].items.empty() && categories_[active_category_].items[0].is_separator) {
			find_next_item();
		}
		event_logger::get_instance().log("Menu activated: " + categories_[active_category_].name);
		event_logger::get_instance().log("Menu key: " + std::to_string(key));
		return true;
	} else if (key == KEY_LEFT) {
		active_category_ = (active_category_ - 1 + categories_.size()) % categories_.size();
		selected_item_ = 0;
		if (!categories_[active_category_].items.empty() && categories_[active_category_].items[0].is_separator) {
			find_next_item();
		}
		event_logger::get_instance().log("Menu activated: " + categories_[active_category_].name);
		event_logger::get_instance().log("Menu key: " + std::to_string(key));
		return true;
	} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
		if (!categories_[active_category_].items.empty()) {
			editor_event ev;
			const auto &item = categories_[active_category_].items[selected_item_];
			ev.type = item.action;
			ev.key_code = item.action_key_code;
			event_logger::get_instance().log("Menu pushing event: " + std::to_string(static_cast<int>(ev.type)));
			queue.push(ev);
		}
		close_menu();
		return true;
	} else if (key > 0 && key < 256) {
		char c = std::tolower(static_cast<char>(key));
		const auto &items = categories_[active_category_].items;
		for (size_t i = 0; i < items.size(); ++i) {
			if (!items[i].is_separator && std::tolower(items[i].hotkey) == c) {
				editor_event ev;
				ev.type = items[i].action;
				ev.key_code = items[i].action_key_code;
				event_logger::get_instance().log("Menu hotkey " + std::string(1, c) +
								 " pushing event: " + std::to_string(static_cast<int>(ev.type)));
				queue.push(ev);
				close_menu();
				return true;
			}
		}
	}

	return true; // Consume other keys while menu is open
}

void menu_bar::draw() const
{
	move(0, 0);
	attron(COLOR_PAIR(1));
	for (int i = 0; i < COLS; ++i) {
		addch(' ');
	}

	int col = 1;
	std::vector<int> category_cols;

	for (size_t i = 0; i < categories_.size(); ++i) {
		category_cols.push_back(col);
		move(0, col);

		bool is_active = (active_category_ == static_cast<int>(i));
		if (is_active)
			attrset(COLOR_PAIR(14));
		else
			attrset(COLOR_PAIR(1));

		addch(' ');

		// Find hotkey position in category name
		size_t hotkey_pos = std::string::npos;
		std::string lower_name = categories_[i].name;
		for (char &c : lower_name)
			c = std::tolower(c);
		hotkey_pos = lower_name.find(std::tolower(categories_[i].hotkey));

		for (size_t j = 0; j < categories_[i].name.length(); ++j) {
			if (j == hotkey_pos) {
				if (is_active)
					attron(COLOR_PAIR(15));
				else
					attron(COLOR_PAIR(2));
				addch(categories_[i].name[j]);
				if (is_active)
					attron(COLOR_PAIR(14));
				else
					attron(COLOR_PAIR(1));
			} else {
				addch(categories_[i].name[j]);
			}
		}

		addch(' ');
		if (is_active)
			attrset(0);

		col += 2 + categories_[i].name.length();
	}
	attrset(0);

	if (active_category_ != -1) {
		const auto &cat = categories_[active_category_];
		int drop_col = category_cols[active_category_];

		int drop_width = 0;
		for (const auto &item : cat.items) {
			int w = item.name.length() + item.shortcut.length() + 4;
			if (w > drop_width)
				drop_width = w;
		}
		if (drop_width < 15)
			drop_width = 15;
		int drop_height = cat.items.size() + 2;

		// Draw shadow
		attron(COLOR_PAIR(6));
		for (int i = 0; i < drop_height; ++i)
			mvaddch(2 + i, drop_col + drop_width, ' ');
		for (int i = 0; i < drop_width; ++i)
			mvaddch(drop_height + 1, drop_col + 1 + i, ' ');
		attroff(COLOR_PAIR(6));

		attron(COLOR_PAIR(1));
		mvaddstr(1, drop_col, "┌");
		for (int j = 1; j < drop_width - 1; ++j)
			addstr("─");
		addstr("┐");

		for (size_t i = 0; i < cat.items.size(); ++i) {
			const auto &item = cat.items[i];
			if (item.is_separator) {
				mvaddstr(2 + i, drop_col, "├");
				for (int j = 1; j < drop_width - 1; ++j)
					addstr("─");
				addstr("┤");
			} else {
				bool selected = (static_cast<int>(i) == selected_item_);
				mvaddstr(2 + i, drop_col, "│");
				if (selected)
					attrset(COLOR_PAIR(14));
				else
					attrset(COLOR_PAIR(1));

				// Background fill
				for (int j = 1; j < drop_width - 1; ++j)
					mvaddch(2 + i, drop_col + j, ' ');
				move(2 + i, drop_col + 1);

				// Find hotkey position
				size_t hotkey_pos = std::string::npos;
				if (item.hotkey != 0) {
					std::string lower_name = item.name;
					for (char &c : lower_name)
						c = std::tolower(c);
					hotkey_pos = lower_name.find(std::tolower(item.hotkey));
				}

				// Draw name with hotkey
				for (size_t j = 0; j < item.name.length(); ++j) {
					if (j == hotkey_pos) {
						if (selected)
							attron(COLOR_PAIR(15));
						else
							attron(COLOR_PAIR(2));

						addch(item.name[j]);

						if (selected)
							attron(COLOR_PAIR(14));
						else
							attron(COLOR_PAIR(1));
					} else {
						addch(item.name[j]);
					}
				}

				// Draw shortcut right-aligned
				if (!item.shortcut.empty()) {
					int shortcut_x = drop_col + drop_width - 1 - item.shortcut.length() - 1;
					mvaddstr(2 + i, shortcut_x, item.shortcut.c_str());
				}

				attrset(COLOR_PAIR(1));
				mvaddstr(2 + i, drop_col + drop_width - 1, "│");
			}
		}

		mvaddstr(2 + cat.items.size(), drop_col, "└");
		for (int j = 1; j < drop_width - 1; ++j)
			addstr("─");
		addstr("┘");
		attroff(COLOR_PAIR(1));
	}
}

void menu_bar::find_next_item()
{
	if (active_category_ == -1 || categories_[active_category_].items.empty())
		return;
	int start_item = selected_item_;
	do {
		selected_item_ = (selected_item_ + 1) % categories_[active_category_].items.size();
	} while (categories_[active_category_].items[selected_item_].is_separator && selected_item_ != start_item);
}

void menu_bar::find_prev_item()
{
	if (active_category_ == -1 || categories_[active_category_].items.empty())
		return;
	int start_item = selected_item_;
	do {
		selected_item_ =
		    (selected_item_ - 1 + categories_[active_category_].items.size()) % categories_[active_category_].items.size();
	} while (categories_[active_category_].items[selected_item_].is_separator && selected_item_ != start_item);
}

bool menu_bar::handle_mouse(int x, int y, event_queue &queue)
{
	if (active_category_ != -1 && y > 0) {
		const auto &cat = categories_[active_category_];
		
		int col = 1;
		int drop_col = 1;
		for (int i = 0; i < active_category_; ++i) {
			col += 2 + categories_[i].name.length();
		}
		drop_col = col;

		int drop_width = 0;
		for (const auto &item : cat.items) {
			int w = item.name.length() + item.shortcut.length() + 4;
			if (w > drop_width)
				drop_width = w;
		}
		if (drop_width < 15)
			drop_width = 15;

		if (y >= 2 && y < 2 + static_cast<int>(cat.items.size()) && x >= drop_col && x < drop_col + drop_width) {
			int clicked_idx = y - 2;
			if (!cat.items[clicked_idx].is_separator) {
				selected_item_ = clicked_idx;
				editor_event ev;
				const auto &item = cat.items[selected_item_];
				ev.type = item.action;
				ev.key_code = item.action_key_code;
				event_logger::get_instance().log("Menu (mouse) pushing event: " + std::to_string(static_cast<int>(ev.type)));
				queue.push(ev);
				close_menu();
			}
			return true;
		} else {
			// Clicked outside dropdown, close menu
			close_menu();
			// If it was on the menu bar itself, we fall through to let it open another category
			if (y > 0) {
				return true; // Consume the click that closed the menu
			}
		}
	}

	if (y == 0) {
		int col = 1;
		for (size_t i = 0; i < categories_.size(); ++i) {
			int width = 2 + categories_[i].name.length();
			if (x >= col && x < col + width) {
				if (active_category_ == static_cast<int>(i)) {
					// Toggle off
					close_menu();
				} else {
					active_category_ = static_cast<int>(i);
					selected_item_ = 0;
					if (!categories_[active_category_].items.empty() && categories_[active_category_].items[0].is_separator) {
						find_next_item();
					}
					event_logger::get_instance().log("Menu activated (mouse): " + categories_[active_category_].name);
				}
				return true;
			}
			col += width;
		}
		
		// Clicked on empty space in menu bar
		if (active_category_ != -1) {
			close_menu();
			return true;
		}
	}

	return false;
}
