#include "settings_dialog.h"
#include <algorithm>
#include <cctype>
#include <ncurses.h>
#include "config_manager.h"

settings_dialog::settings_dialog()
    : dialog("Preferences", 60, 21)
{
	styles_ = {"LLVM", "Google", "Chromium", "Mozilla", "WebKit", "Microsoft", "GNU", "file"};
	std::string current_style = config_manager::get_instance().get_clang_format_style();
	auto it = std::find(styles_.begin(), styles_.end(), current_style);
	if (it != styles_.end()) {
		selected_style_idx_ = std::distance(styles_.begin(), it);
	}
	
	build_systems_ = {"meson", "cmake", "make"};
	std::string current_system = config_manager::get_instance().get_build_system();
	auto it_sys = std::find(build_systems_.begin(), build_systems_.end(), current_system);
	if (it_sys != build_systems_.end()) {
		selected_build_system_idx_ = std::distance(build_systems_.begin(), it_sys);
	}
	
	build_directory_buffer_ = config_manager::get_instance().get_build_directory();
}

std::string settings_dialog::get_build_system() const
{
	return build_systems_[selected_build_system_idx_];
}

std::string settings_dialog::get_build_directory() const
{
	return build_directory_buffer_;
}

bool settings_dialog::is_lsp_enabled() const
{
	return lsp_enabled_;
}

void settings_dialog::draw_group_box(int gy, int gx, int gw, int gh, const std::string &gtitle) const
{
	// Fill background (Cyan)
	attrset(COLOR_PAIR(17));
	for (int i = 1; i < gh; ++i) {
		move(y_ + gy + i, x_ + gx);
		for (int j = 0; j < gw; ++j)
			addch(' ');
	}

	// Double line border for group box? Actually screenshot uses single lines for internal groups usually, 
	// but let's see. In Turbo C++, it's often a single line.
	// Let's just use a simple border for now.
	for (int i = 1; i < gh; ++i) {
		mvaddch(y_ + gy + i, x_ + gx, ACS_VLINE);
		mvaddch(y_ + gy + i, x_ + gx + gw - 1, ACS_VLINE);
	}
	for (int j = 1; j < gw - 1; ++j) {
		mvaddch(y_ + gy, x_ + gx + j, ACS_HLINE);
		mvaddch(y_ + gy + gh, x_ + gx + j, ACS_HLINE);
	}
	mvaddch(y_ + gy, x_ + gx, ACS_ULCORNER);
	mvaddch(y_ + gy, x_ + gx + gw - 1, ACS_URCORNER);
	mvaddch(y_ + gy + gh, x_ + gx, ACS_LLCORNER);
	mvaddch(y_ + gy + gh, x_ + gx + gw - 1, ACS_LRCORNER);

	if (!gtitle.empty()) {
		attrset(COLOR_PAIR(1)); // Black on White (Gray)
		mvaddstr(y_ + gy, x_ + gx + 2, gtitle.c_str());
	}
}

void settings_dialog::draw_radio_button(int gy, int gx, const std::string &label, bool selected, char hotkey) const
{
	move(y_ + gy, x_ + gx);
	bool group_focused = (focus_idx_ == 0);
	
	// Focus highlight for the selected item if group is focused
	bool item_focused = group_focused && (selected_style_idx_ == (gy - 3)); // Heuristic for this specific dialog

	if (item_focused)
		attrset(COLOR_PAIR(19)); // Black on Green
	else
		attrset(COLOR_PAIR(17)); // Black on Cyan

	addch('(');
	if (selected)
		addstr("•");
	else
		addch(' ');
	addch(')');
	addch(' ');

	// Draw label with hotkey
	std::string lower_label = label;
	for (char &c : lower_label) c = std::tolower(c);
	size_t hotkey_pos = lower_label.find(std::tolower(hotkey));

	for (size_t i = 0; i < label.length(); ++i) {
		if (i == hotkey_pos) {
			if (item_focused)
				attrset(COLOR_PAIR(19)); // No special hotkey color when focused? Or maybe Pair 15?
			else
				attrset(COLOR_PAIR(18)); // Bright Yellow on Cyan
			addch(label[i]);
			if (item_focused)
				attrset(COLOR_PAIR(19));
			else
				attrset(COLOR_PAIR(17));
		} else {
			addch(label[i]);
		}
	}
	attrset(0);
}

void settings_dialog::draw_checkbox(int gy, int gx, const std::string &label, bool checked, char hotkey) const
{
	move(y_ + gy, x_ + gx);
	bool is_focused = (focus_idx_ == 3);
	if (is_focused)
		attrset(COLOR_PAIR(19));
	else
		attrset(COLOR_PAIR(17));

	addch('[');
	if (checked)
		addstr("X");
	else
		addch(' ');
	addch(']');
	addch(' ');

	// Hotkey handling
	std::string lower_label = label;
	for (char &c : lower_label) c = std::tolower(c);
	size_t hotkey_pos = lower_label.find(std::tolower(hotkey));

	for (size_t i = 0; i < label.length(); ++i) {
		if (i == hotkey_pos) {
			if (is_focused)
				attrset(COLOR_PAIR(19));
			else
				attrset(COLOR_PAIR(18));
			addch(label[i]);
			if (is_focused) attrset(COLOR_PAIR(19)); else attrset(COLOR_PAIR(17));
		} else {
			addch(label[i]);
		}
	}
	attrset(0);
}

void settings_dialog::draw_text_input(int gy, int gx, int gw, const std::string &label, const std::string &value, bool focused) const
{
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + gy, x_ + gx, label.c_str());
	
	if (focused) {
		attrset(COLOR_PAIR(19)); // Focused widget background
	} else {
		attrset(COLOR_PAIR(1)); // Normal
	}

	int input_x = gx + label.length() + 1;
	int input_w = gw - label.length() - 1;
	
	move(y_ + gy, x_ + input_x);
	for (int i = 0; i < input_w; ++i) addch(' ');
	
	std::string display_val = value;
	if (display_val.length() > static_cast<size_t>(input_w - 1)) {
		display_val = display_val.substr(display_val.length() - input_w + 1);
	}
	mvaddstr(y_ + gy, x_ + input_x, display_val.c_str());
	
	if (focused) {
		// Draw a pseudo-cursor
		attrset(COLOR_PAIR(1));
		mvaddch(y_ + gy, x_ + input_x + display_val.length(), '_');
	}
	attrset(0);
}

void settings_dialog::draw() const
{
	dialog::draw();
	
	// Clang Format Style group
	draw_group_box(2, 4, 30, 9, " Clang Format Style ");
	
	std::vector<std::pair<std::string, char>> style_labels = {
		{"LLVM", 'L'},
		{"Google", 'G'},
		{"Chromium", 'C'},
		{"Mozilla", 'M'},
		{"WebKit", 'W'},
		{"Microsoft", 's'},
		{"GNU", 'N'},
		{".clang-format file", 'f'}
	};

	for (size_t i = 0; i < style_labels.size(); ++i) {
		draw_radio_button(3 + static_cast<int>(i), 6, style_labels[i].first, static_cast<int>(i) == selected_style_idx_, style_labels[i].second);
	}

	// Build System group
	draw_group_box(2, 36, 20, 5, " Build System ");
	
	std::vector<std::pair<std::string, char>> system_labels = {
		{"meson", 'm'},
		{"cmake", 'k'},
		{"make", 'a'}
	};

	for (size_t i = 0; i < system_labels.size(); ++i) {
		bool is_focused = (focus_idx_ == 1);
		bool is_selected = (static_cast<int>(i) == selected_build_system_idx_);
		int gy = 3 + static_cast<int>(i);
		int gx = 38;
		
		move(y_ + gy, x_ + gx);
		bool item_focused = is_focused && is_selected;

		if (item_focused)
			attrset(COLOR_PAIR(19));
		else
			attrset(COLOR_PAIR(17));

		addch('(');
		if (is_selected) addstr("•"); else addch(' ');
		addch(')');
		addch(' ');

		std::string label = system_labels[i].first;
		char hotkey = system_labels[i].second;
		size_t hotkey_pos = label.find(std::tolower(hotkey));

		for (size_t j = 0; j < label.length(); ++j) {
			if (j == hotkey_pos) {
				attron(COLOR_PAIR(item_focused ? 19 : 18));
				addch(label[j]);
				attroff(COLOR_PAIR(item_focused ? 19 : 18));
				attron(COLOR_PAIR(item_focused ? 19 : 17));
			} else {
				addch(label[j]);
			}
		}
		attrset(0);
	}

	// Build Directory Input
	draw_text_input(13, 4, 52, "Build Directory:", build_directory_buffer_, focus_idx_ == 2);

	// LSP toggle
	draw_checkbox(15, 4, "Enable LSP (clangd)", lsp_enabled_, 'E');

	// Buttons
	auto draw_btn = [&](int by, int bx, const std::string &btext, char bhot, bool focused) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + by, x_ + bx + btext.length(), "▄");
		for (size_t i = 0; i < btext.length(); ++i)
			mvaddstr(y_ + by + 1, x_ + bx + 1 + i, "▀");
		
		move(y_ + by, x_ + bx);
		if (focused)
			attrset(COLOR_PAIR(14));
		else
			attrset(COLOR_PAIR(10));
			
		for (size_t i = 0; i < btext.length(); ++i) {
			if (std::tolower(btext[i]) == std::tolower(bhot)) {
				attrset(COLOR_PAIR(15));
				addch(btext[i]);
				if (focused) attrset(COLOR_PAIR(14)); else attrset(COLOR_PAIR(10));
			} else {
				addch(btext[i]);
			}
		}
		attrset(0);
	};

	draw_btn(18, 10, "  OK  ", 'o', focus_idx_ == 4);
	draw_btn(18, 25, " Cancel ", 'c', focus_idx_ == 5);
	draw_btn(18, 40, " Help ", 'h', focus_idx_ == 6);

	attrset(0);
}

dialog_result settings_dialog::handle_key(int key)
{
	if (key == 27) return dialog_result::cancelled;
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		if (focus_idx_ == 4) return dialog_result::confirmed;
		if (focus_idx_ == 5) return dialog_result::cancelled;
		if (focus_idx_ <= 3) return dialog_result::confirmed;
	}

	if (key == '\t') {
		focus_idx_ = (focus_idx_ + 1) % 7;
		return dialog_result::pending;
	}
	if (key == KEY_BTAB) {
		focus_idx_ = (focus_idx_ - 1 + 7) % 7;
		return dialog_result::pending;
	}

	if (focus_idx_ == 0) {
		if (key == KEY_UP) {
			selected_style_idx_ = (selected_style_idx_ - 1 + static_cast<int>(styles_.size())) % static_cast<int>(styles_.size());
		} else if (key == KEY_DOWN) {
			selected_style_idx_ = (selected_style_idx_ + 1) % static_cast<int>(styles_.size());
		}
	} else if (focus_idx_ == 1) {
		if (key == KEY_UP) {
			selected_build_system_idx_ = (selected_build_system_idx_ - 1 + static_cast<int>(build_systems_.size())) % static_cast<int>(build_systems_.size());
		} else if (key == KEY_DOWN) {
			selected_build_system_idx_ = (selected_build_system_idx_ + 1) % static_cast<int>(build_systems_.size());
		}
	} else if (focus_idx_ == 2) {
		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!build_directory_buffer_.empty()) build_directory_buffer_.pop_back();
			return dialog_result::pending;
		}
		if (key >= 32 && key <= 126) {
			build_directory_buffer_ += static_cast<char>(key);
			return dialog_result::pending;
		}
	} else if (focus_idx_ == 3) {
		if (key == ' ') {
			lsp_enabled_ = !lsp_enabled_;
			return dialog_result::pending;
		}
	} else {
		if (key == KEY_LEFT || key == KEY_RIGHT) {
			if (focus_idx_ >= 4 && focus_idx_ <= 6) {
				if (key == KEY_RIGHT) focus_idx_ = ((focus_idx_ - 4 + 1) % 3) + 4;
				else focus_idx_ = ((focus_idx_ - 4 + 2) % 3) + 4;
			}
		}
	}

	int k = std::tolower(key);
	if (k == 'l') { selected_style_idx_ = 0; focus_idx_ = 0; }
	else if (k == 'g') { selected_style_idx_ = 1; focus_idx_ = 0; }
	else if (k == 'c') { 
		if (focus_idx_ != 5) { // Not Cancel hotkey
			selected_style_idx_ = 2; focus_idx_ = 0; 
		}
	}
	else if (k == 'm') { selected_style_idx_ = 3; focus_idx_ = 0; }
	else if (k == 'w') { selected_style_idx_ = 4; focus_idx_ = 0; }
	else if (k == 's') { selected_style_idx_ = 5; focus_idx_ = 0; }
	else if (k == 'n') { selected_style_idx_ = 6; focus_idx_ = 0; }
	else if (k == 'f') { selected_style_idx_ = 7; focus_idx_ = 0; }
	else if (k == 'e') { lsp_enabled_ = !lsp_enabled_; focus_idx_ = 3; }
	else if (k == 'k' && focus_idx_ != 3) { selected_build_system_idx_ = 1; focus_idx_ = 1; }
	else if (k == 'a' && focus_idx_ != 3) { selected_build_system_idx_ = 2; focus_idx_ = 1; }
	else if (k == 'o' && focus_idx_ >= 4) return dialog_result::confirmed;

	return dialog_result::pending;
}

std::string settings_dialog::get_selected_style() const
{
	return styles_[selected_style_idx_];
}
