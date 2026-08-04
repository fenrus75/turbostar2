#include "ui/menu_bar.h"
#include <cctype>
#include <format>
#include <ncurses.h>
#include "event_logger.h"

menu_bar::menu_bar()
{
	categories_ = {
	    {"File",
	     'f',
	     {{"New Project...", event_type::new_project, 'p', "", false},
	      {"New File", event_type::new_doc, 'n', "^KN", false},
	      {"Open...", event_type::load, 'o', "^KE", false},
	      menu_item("Open Recent...", {}, 'r'),
	      {"Save", event_type::save, 's', "^KS", false},
	      {"Save as...", event_type::save_as, 'a', "^KW", false},
	      {"Save All", event_type::save_all, 'v', "^KA", false},
	      {"Close", event_type::close_window, 'c', "Alt+F3", false},
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
	      {"Run Tests", event_type::run_tests, 't', "F8", false},
	      {"View Crashdumps", event_type::open_crashdump_viewer, 'C', "", false},
	      {"View Code Reviews", event_type::open_codereview_viewer, 'R', "", false}}},
	    {"Run",
	     'r',
	     {{"Run", event_type::run_program, 'r', "Ctrl+F9", false},
	      {"Run in Debugger", event_type::run_in_debugger, 'd', "Ctrl+Shift+F9", false},
	      {"Run (Performance profile)", event_type::run_profile, 'p', "", false},
	      {"Go to next hotspot", event_type::go_to_next_hotspot, 'h', "F7", false},
	      {"Run Settings...", event_type::run_settings, 's', "", false}}},
	    {"Options",
	     'p',
	     {{"Editor Settings...", event_type::editor_settings_config, 'E', "", false},
	      {"AI & Agent Settings...", event_type::ai_settings_config, 'A', "", false},
	      {"A2A & Remote Settings...", event_type::a2a_settings_config, 'N', "", false}}},
	    {"Git", 'g', {{"Git add", event_type::git_add, 'a', "", false}, {"Git refresh", event_type::git_refresh, 'r', "", false}}},
	    {"Agent",
	     'a',
	     {{"Open Chat...", event_type::open_agent, 'o', "", false},
	      {"Command Center...", event_type::open_agent_center, 'c', "", false},
	      {"Select Model...", event_type::agent_switch_model, 's', "", false},
	      {"Image VFS Manager...", event_type::image_manager, 'i', "", false},
	      {"Rescan Subagents", event_type::rescan_subagents, 'r', "", false}}},
	    {"Window", 'w', {}},
	    {"Help",
	     'h',
	     {{"Key bindings", event_type::help, 'k', "F1", false},
	      {"Tool status", event_type::tool_status, 't', "", false},
	      {"Plugins...", event_type::plugins, 'p', "", false},
	      {"About...", event_type::about, 'a', "", false}}}};
}

void menu_bar::select_category(int index)
{
	if (index < 0 || index >= static_cast<int>(categories_.size())) {
		active_category_ = -1;
		selected_item_ = 0;
		submenu_open_ = false;
		selected_submenu_item_ = 0;
		return;
	}
	active_category_ = index;
	selected_item_ = 0;
	submenu_open_ = false;
	selected_submenu_item_ = 0;
	const auto &items = categories_[active_category_].items;
	if (!items.empty() && (items[0].is_separator || items[0].is_disabled)) {
		find_next_item();
	}
}

bool menu_bar::handle_alt_key(char c, event_queue &queue)
{
	(void)queue;
	c = std::tolower(c);
	for (size_t i = 0; i < categories_.size(); ++i) {
		if (categories_[i].hotkey == c) {
			select_category(static_cast<int>(i));
			event_logger::get_instance().log("Menu activated: {}", categories_[i].name);
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
	submenu_open_ = false;
	selected_submenu_item_ = 0;
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

void menu_bar::set_item_disabled(event_type action, bool disabled)
{
	for (auto &cat : categories_) {
		for (auto &item : cat.items) {
			if (item.action == action) {
				item.is_disabled = disabled;
			}
			for (auto &sub : item.submenu_items) {
				if (sub.action == action) {
					sub.is_disabled = disabled;
				}
			}
		}
	}
}

bool menu_bar::handle_key(int key, event_queue &queue)
{
	if (!is_open())
		return false;
	event_logger::get_instance().log("Menu handle_key: {}", key);

	if (key == 27) { // ESC
		if (submenu_open_) {
			submenu_open_ = false;
			return true;
		}
		close_menu();
		return true;
	}

	const auto &items = categories_[active_category_].items;
	bool has_valid_selected = (selected_item_ >= 0 && selected_item_ < static_cast<int>(items.size()));

	if (key == KEY_DOWN) {
		if (submenu_open_ && has_valid_selected && items[selected_item_].has_submenu()) {
			find_next_submenu_item();
		} else {
			find_next_item();
		}
		return true;
	} else if (key == KEY_UP) {
		if (submenu_open_ && has_valid_selected && items[selected_item_].has_submenu()) {
			find_prev_submenu_item();
		} else {
			find_prev_item();
		}
		return true;
	} else if (key == KEY_RIGHT) {
		if (!submenu_open_ && has_valid_selected && items[selected_item_].has_submenu() && !items[selected_item_].is_disabled) {
			submenu_open_ = true;
			selected_submenu_item_ = 0;
			const auto &sub = items[selected_item_].submenu_items;
			if (!sub.empty() && (sub[0].is_separator || sub[0].is_disabled)) {
				find_next_submenu_item();
			}
		} else {
			select_category((active_category_ + 1) % categories_.size());
			event_logger::get_instance().log("Menu activated: {}", categories_[active_category_].name);
		}
		return true;
	} else if (key == KEY_LEFT) {
		if (submenu_open_) {
			submenu_open_ = false;
		} else {
			select_category((active_category_ - 1 + categories_.size()) % categories_.size());
			event_logger::get_instance().log("Menu activated: {}", categories_[active_category_].name);
		}
		return true;
	} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
		if (has_valid_selected) {
			const auto &item = items[selected_item_];
			if (submenu_open_ && item.has_submenu()) {
				const auto &sub_items = item.submenu_items;
				if (selected_submenu_item_ >= 0 && selected_submenu_item_ < static_cast<int>(sub_items.size())) {
					const auto &sub_item = sub_items[selected_submenu_item_];
					if (!sub_item.is_disabled && !sub_item.is_separator) {
						editor_event ev;
						ev.type = sub_item.action;
						ev.key_code = sub_item.action_key_code;
						ev.payload = sub_item.payload;
						event_logger::get_instance().log("Menu pushing submenu event: {} payload: {}", static_cast<int>(ev.type), ev.payload);
						queue.push(ev);
						close_menu();
					}
				}
				return true;
			} else if (item.has_submenu()) {
				if (!item.is_disabled) {
					submenu_open_ = true;
					selected_submenu_item_ = 0;
					const auto &sub = item.submenu_items;
					if (!sub.empty() && (sub[0].is_separator || sub[0].is_disabled)) {
						find_next_submenu_item();
					}
				}
				return true;
			} else {
				if (!item.is_disabled) {
					editor_event ev;
					ev.type = item.action;
					ev.key_code = item.action_key_code;
					ev.payload = item.payload;
					event_logger::get_instance().log("Menu pushing event: {}", static_cast<int>(ev.type));
					queue.push(ev);
					close_menu();
				}
				return true;
			}
		}
		close_menu();
		return true;
	} else if (key > 0 && key < 256) {
		char c = std::tolower(static_cast<char>(key));
		if (submenu_open_ && has_valid_selected && items[selected_item_].has_submenu()) {
			const auto &sub_items = items[selected_item_].submenu_items;
			for (size_t i = 0; i < sub_items.size(); ++i) {
				if (!sub_items[i].is_separator && std::tolower(sub_items[i].hotkey) == c) {
					if (sub_items[i].is_disabled) return true;
					editor_event ev;
					ev.type = sub_items[i].action;
					ev.key_code = sub_items[i].action_key_code;
					ev.payload = sub_items[i].payload;
					queue.push(ev);
					close_menu();
					return true;
				}
			}
		}

		for (size_t i = 0; i < items.size(); ++i) {
			if (!items[i].is_separator && std::tolower(items[i].hotkey) == c) {
				if (items[i].is_disabled) return true;
				if (items[i].has_submenu()) {
					selected_item_ = static_cast<int>(i);
					submenu_open_ = true;
					selected_submenu_item_ = 0;
					const auto &sub = items[i].submenu_items;
					if (!sub.empty() && (sub[0].is_separator || sub[0].is_disabled)) {
						find_next_submenu_item();
					}
					return true;
				}
				editor_event ev;
				ev.type = items[i].action;
				ev.key_code = items[i].action_key_code;
				ev.payload = items[i].payload;
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
			if (item.has_submenu()) {
				w += 2;
			}
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
				if (item.is_disabled) {
					attrset(COLOR_PAIR(37));
				} else if (selected) {
					attrset(COLOR_PAIR(14));
				} else {
					attrset(COLOR_PAIR(1));
				}

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
					if (j == hotkey_pos && !item.is_disabled) {
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

				// Draw shortcut or submenu arrow right-aligned
				if (item.has_submenu()) {
					int arrow_x = drop_col + drop_width - 2;
					mvaddstr(2 + i, arrow_x, "►");
				} else if (!item.shortcut.empty()) {
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

		// Draw Submenu Flyout Box if open
		if (submenu_open_ && selected_item_ >= 0 && selected_item_ < static_cast<int>(cat.items.size()) && cat.items[selected_item_].has_submenu()) {
			const auto &sub_items = cat.items[selected_item_].submenu_items;
			int sub_drop_col = drop_col + drop_width - 1;
			int sub_drop_row = 2 + selected_item_;

			int sub_width = 0;
			for (const auto &s_item : sub_items) {
				int w = s_item.name.length() + s_item.shortcut.length() + 4;
				if (w > sub_width) sub_width = w;
			}
			if (sub_width < 15) sub_width = 15;
			int sub_height = sub_items.size() + 2;

			if (sub_drop_col + sub_width >= COLS) {
				sub_drop_col = std::max(1, drop_col - sub_width + 1);
			}
			if (sub_drop_row + sub_height >= LINES - 1) {
				sub_drop_row = std::max(1, static_cast<int>(LINES - 1 - sub_height));
			}

			// Draw shadow
			attron(COLOR_PAIR(6));
			for (int i = 0; i < sub_height; ++i)
				mvaddch(sub_drop_row + i, sub_drop_col + sub_width, ' ');
			for (int i = 0; i < sub_width; ++i)
				mvaddch(sub_drop_row + sub_height, sub_drop_col + 1 + i, ' ');
			attroff(COLOR_PAIR(6));

			// Border
			attron(COLOR_PAIR(1));
			mvaddstr(sub_drop_row, sub_drop_col, "┌");
			for (int j = 1; j < sub_width - 1; ++j)
				addstr("─");
			addstr("┐");

			for (size_t i = 0; i < sub_items.size(); ++i) {
				const auto &s_item = sub_items[i];
				int r = sub_drop_row + 1 + static_cast<int>(i);
				if (s_item.is_separator) {
					mvaddstr(r, sub_drop_col, "├");
					for (int j = 1; j < sub_width - 1; ++j)
						addstr("─");
					addstr("┤");
				} else {
					bool selected = (static_cast<int>(i) == selected_submenu_item_);
					mvaddstr(r, sub_drop_col, "│");
					if (s_item.is_disabled) {
						attrset(COLOR_PAIR(37));
					} else if (selected) {
						attrset(COLOR_PAIR(14));
					} else {
						attrset(COLOR_PAIR(1));
					}

					for (int j = 1; j < sub_width - 1; ++j)
						mvaddch(r, sub_drop_col + j, ' ');
					move(r, sub_drop_col + 1);

					size_t hotkey_pos = std::string::npos;
					if (s_item.hotkey != 0) {
						std::string lower_name = s_item.name;
						for (char &c : lower_name)
							c = std::tolower(c);
						hotkey_pos = lower_name.find(std::tolower(s_item.hotkey));
					}

					for (size_t j = 0; j < s_item.name.length(); ++j) {
						if (j == hotkey_pos && !s_item.is_disabled) {
							if (selected)
								attron(COLOR_PAIR(15));
							else
								attron(COLOR_PAIR(2));

							addch(s_item.name[j]);

							if (selected)
								attron(COLOR_PAIR(14));
							else
								attron(COLOR_PAIR(1));
						} else {
							addch(s_item.name[j]);
						}
					}

					if (!s_item.shortcut.empty()) {
						int shortcut_x = sub_drop_col + sub_width - 1 - s_item.shortcut.length() - 1;
						mvaddstr(r, shortcut_x, s_item.shortcut.c_str());
					}

					attrset(COLOR_PAIR(1));
					mvaddstr(r, sub_drop_col + sub_width - 1, "│");
				}
			}

			mvaddstr(sub_drop_row + 1 + sub_items.size(), sub_drop_col, "└");
			for (int j = 1; j < sub_width - 1; ++j)
				addstr("─");
			addstr("┘");
			attroff(COLOR_PAIR(1));
		}
	}
}

void menu_bar::find_next_item()
{
	if (active_category_ == -1 || categories_[active_category_].items.empty())
		return;
	int start_item = selected_item_;
	do {
		selected_item_ = (selected_item_ + 1) % categories_[active_category_].items.size();
	} while ((categories_[active_category_].items[selected_item_].is_separator ||
	          categories_[active_category_].items[selected_item_].is_disabled) &&
	         selected_item_ != start_item);
	submenu_open_ = false;
	selected_submenu_item_ = 0;
}

void menu_bar::find_prev_item()
{
	if (active_category_ == -1 || categories_[active_category_].items.empty())
		return;
	int start_item = selected_item_;
	do {
		selected_item_ =
		    (selected_item_ - 1 + categories_[active_category_].items.size()) % categories_[active_category_].items.size();
	} while ((categories_[active_category_].items[selected_item_].is_separator ||
	          categories_[active_category_].items[selected_item_].is_disabled) &&
	         selected_item_ != start_item);
	submenu_open_ = false;
	selected_submenu_item_ = 0;
}

void menu_bar::find_next_submenu_item()
{
	if (active_category_ == -1 || selected_item_ < 0 || selected_item_ >= static_cast<int>(categories_[active_category_].items.size()))
		return;
	const auto &sub_items = categories_[active_category_].items[selected_item_].submenu_items;
	if (sub_items.empty()) return;
	int start_item = selected_submenu_item_;
	do {
		selected_submenu_item_ = (selected_submenu_item_ + 1) % sub_items.size();
	} while ((sub_items[selected_submenu_item_].is_separator || sub_items[selected_submenu_item_].is_disabled) && selected_submenu_item_ != start_item);
}

void menu_bar::find_prev_submenu_item()
{
	if (active_category_ == -1 || selected_item_ < 0 || selected_item_ >= static_cast<int>(categories_[active_category_].items.size()))
		return;
	const auto &sub_items = categories_[active_category_].items[selected_item_].submenu_items;
	if (sub_items.empty()) return;
	int start_item = selected_submenu_item_;
	do {
		selected_submenu_item_ = (selected_submenu_item_ - 1 + sub_items.size()) % sub_items.size();
	} while ((sub_items[selected_submenu_item_].is_separator || sub_items[selected_submenu_item_].is_disabled) && selected_submenu_item_ != start_item);
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
			if (item.has_submenu()) w += 2;
			if (w > drop_width)
				drop_width = w;
		}
		if (drop_width < 15)
			drop_width = 15;

		// Check if click was inside Submenu Flyout Box
		if (submenu_open_ && selected_item_ >= 0 && selected_item_ < static_cast<int>(cat.items.size()) && cat.items[selected_item_].has_submenu()) {
			const auto &sub_items = cat.items[selected_item_].submenu_items;
			int sub_drop_col = drop_col + drop_width - 1;
			int sub_drop_row = 2 + selected_item_;
			int sub_width = 0;
			for (const auto &s_item : sub_items) {
				int w = s_item.name.length() + s_item.shortcut.length() + 4;
				if (w > sub_width) sub_width = w;
			}
			if (sub_width < 15) sub_width = 15;
			int sub_height = sub_items.size() + 2;

			if (sub_drop_col + sub_width >= COLS) {
				sub_drop_col = std::max(1, drop_col - sub_width + 1);
			}
			if (sub_drop_row + sub_height >= LINES - 1) {
				sub_drop_row = std::max(1, static_cast<int>(LINES - 1 - sub_height));
			}

			if (y >= sub_drop_row + 1 && y < sub_drop_row + 1 + static_cast<int>(sub_items.size()) && x >= sub_drop_col && x < sub_drop_col + sub_width) {
				int clicked_idx = y - (sub_drop_row + 1);
				if (!sub_items[clicked_idx].is_separator && !sub_items[clicked_idx].is_disabled) {
					selected_submenu_item_ = clicked_idx;
					editor_event ev;
					ev.type = sub_items[selected_submenu_item_].action;
					ev.key_code = sub_items[selected_submenu_item_].action_key_code;
					ev.payload = sub_items[selected_submenu_item_].payload;
					queue.push(ev);
					close_menu();
				}
				return true;
			}
		}

		if (y >= 2 && y < 2 + static_cast<int>(cat.items.size()) && x >= drop_col && x < drop_col + drop_width) {
			int clicked_idx = y - 2;
			if (!cat.items[clicked_idx].is_separator) {
				selected_item_ = clicked_idx;
				if (cat.items[selected_item_].has_submenu()) {
					if (!cat.items[selected_item_].is_disabled) {
						submenu_open_ = true;
						selected_submenu_item_ = 0;
						find_next_submenu_item();
					}
				} else {
					editor_event ev;
					const auto &item = cat.items[selected_item_];
					ev.type = item.action;
					ev.key_code = item.action_key_code;
					ev.payload = item.payload;
					event_logger::get_instance().log("Menu (mouse) pushing event: {}", static_cast<int>(ev.type));
					queue.push(ev);
					close_menu();
				}
			}
			return true;
		} else {
			close_menu();
			if (y > 0) {
				return true;
			}
		}
	}

	if (y == 0) {
		int col = 1;
		for (size_t i = 0; i < categories_.size(); ++i) {
			int width = 2 + categories_[i].name.length();
			if (x >= col && x < col + width) {
				if (active_category_ == static_cast<int>(i)) {
					close_menu();
				} else {
					select_category(static_cast<int>(i));
					event_logger::get_instance().log("Menu activated (mouse): {}", categories_[active_category_].name);
				}
				return true;
			}
			col += width;
		}

		if (active_category_ != -1) {
			close_menu();
			return true;
		}
	}

	return false;
}
