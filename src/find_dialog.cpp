#include "find_dialog.h"
#include <ncurses.h>
#include <cctype>

find_dialog::find_dialog(const std::string& title, const search_params& initial_params, bool is_replace)
	: dialog(title, 64, is_replace ? 18 : 16), params_(initial_params), is_replace_(is_replace)
{
}

void find_dialog::draw_group_box(int gy, int gx, int gw, int gh, const std::string& gtitle) const
{
	// Fill background
	attrset(COLOR_PAIR(17));
	for (int i = 1; i < gh; ++i) {
		move(y_ + gy + i, x_ + gx);
		for (int j = 0; j < gw; ++j) addch(' ');
	}

	if (!gtitle.empty()) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + gy, x_ + gx, gtitle.c_str());
	}
	attrset(0);
}

void find_dialog::draw_labeled_text(int ly, int lx, const std::string& text, char hotkey) const
{
	std::string lower_text = text;
	for(char &c : lower_text) c = std::tolower(c);
	size_t hotkey_pos = lower_text.find(std::tolower(hotkey));

	move(y_ + ly, x_ + lx);
	for (size_t i = 0; i < text.length(); ++i) {
		if (i == hotkey_pos) {
			attrset(COLOR_PAIR(16)); // Bright Yellow on Gray
			addch(text[i]);
			attrset(COLOR_PAIR(1));
		} else {
			addch(text[i]);
		}
	}
}

void find_dialog::draw_group_labeled_text(int ly, int lx, const std::string& text, char hotkey) const
{
	std::string lower_text = text;
	for(char &c : lower_text) c = std::tolower(c);
	size_t hotkey_pos = lower_text.find(std::tolower(hotkey));

	move(y_ + ly, x_ + lx);
	attrset(COLOR_PAIR(17));
	for (size_t i = 0; i < text.length(); ++i) {
		if (i == hotkey_pos) {
			attrset(COLOR_PAIR(18)); // Bright Yellow on Cyan
			addch(text[i]);
			attrset(COLOR_PAIR(17));
		} else {
			addch(text[i]);
		}
	}
}

void find_dialog::draw() const
{
	dialog::draw();
	attrset(COLOR_PAIR(1));
	
	int y_off = is_replace_ ? 2 : 0;

	// Label: Text to find
	draw_labeled_text(2, 2, "Text to find", 't');
	
	// Input field 1
	attrset(COLOR_PAIR(3)); 
	move(y_ + 2, x_ + 16);
	for (int i = 0; i < 40; ++i) addch(' ');
	mvaddstr(y_ + 2, x_ + 16, params_.query.c_str());
	if (focus_idx_ == 0) {
		move(y_ + 2, x_ + 16 + params_.query.length());
		attrset(COLOR_PAIR(19)); addch(' '); attrset(COLOR_PAIR(3));
	}
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 2, x_ + 56, "[↓]");

	if (is_replace_) {
		draw_labeled_text(4, 2, "Replace with", 'n');
		attrset(COLOR_PAIR(3));
		move(y_ + 4, x_ + 16);
		for (int i = 0; i < 40; ++i) addch(' ');
		mvaddstr(y_ + 4, x_ + 16, params_.replacement.c_str());
		if (focus_idx_ == 1) {
			move(y_ + 4, x_ + 16 + params_.replacement.length());
			attrset(COLOR_PAIR(19)); addch(' '); attrset(COLOR_PAIR(3));
		}
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + 4, x_ + 56, "[↓]");
	}

	// Groups
	draw_group_box(5 + y_off, 2, 30, is_replace_ ? 5 : 4, "Options");
	draw_group_box(5 + y_off, 33, 28, 3, "Direction");
	draw_group_box(10 + y_off, 2, 30, 3, "Scope");
	draw_group_box(10 + y_off, 33, 28, 3, "Origin");

	// Focus indicator helpers
	auto draw_checkbox = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused) attrset(COLOR_PAIR(19));
		else attrset(COLOR_PAIR(17));
		addch('[');
		if (val) {
			if (focused) attrset(COLOR_PAIR(19));
			else attrset(COLOR_PAIR(11)); 
			addch('X');
			attrset(focused ? COLOR_PAIR(19) : COLOR_PAIR(17));
		} else addch(' ');
		addch(']');
		attrset(0);
	};

	auto draw_radio = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused) attrset(COLOR_PAIR(19));
		else attrset(COLOR_PAIR(17));
		addch('(');
		if (val) {
			if (focused) attrset(COLOR_PAIR(19));
			else attrset(COLOR_PAIR(11));
			addstr("•");
			attrset(focused ? COLOR_PAIR(19) : COLOR_PAIR(17));
		} else addch(' ');
		addch(')');
		attrset(0);
	};

	int f_off = is_replace_ ? 1 : 0;

	// Options content
	draw_checkbox(6 + y_off, 4, !params_.ignore_case, focus_idx_ == 1 + f_off);
	draw_group_labeled_text(6 + y_off, 8, "Case sensitive", 'c');
	draw_checkbox(7 + y_off, 4, params_.whole_words, focus_idx_ == 2 + f_off);
	draw_group_labeled_text(7 + y_off, 8, "Whole words only", 'w');
	draw_checkbox(8 + y_off, 4, params_.regex, focus_idx_ == 3 + f_off);
	draw_group_labeled_text(8 + y_off, 8, "Regular expression", 'r');
	if (is_replace_) {
		draw_checkbox(9 + y_off, 4, params_.prompt_on_replace, focus_idx_ == 5);
		draw_group_labeled_text(9 + y_off, 8, "Prompt on replace", 'p');
	}

	// Direction content
	draw_radio(6 + y_off, 35, !params_.backward, focus_idx_ == (is_replace_ ? 6 : 4));
	draw_group_labeled_text(6 + y_off, 39, "Forward", 'f');
	draw_radio(7 + y_off, 35, params_.backward, focus_idx_ == (is_replace_ ? 7 : 5));
	draw_group_labeled_text(7 + y_off, 39, "Backward", 'b');

	// Scope content
	draw_radio(11 + y_off, 4, !params_.selected_text_only, focus_idx_ == (is_replace_ ? 8 : 6));
	draw_group_labeled_text(11 + y_off, 8, "Global", 'g');
	draw_radio(12 + y_off, 4, params_.selected_text_only, focus_idx_ == (is_replace_ ? 9 : 7));
	draw_group_labeled_text(12 + y_off, 8, "Selected text", 's');

	// Origin content
	draw_radio(11 + y_off, 35, params_.from_cursor, focus_idx_ == (is_replace_ ? 10 : 8));
	draw_group_labeled_text(11 + y_off, 39, "From cursor", 'o');
	draw_radio(12 + y_off, 35, !params_.from_cursor, focus_idx_ == (is_replace_ ? 11 : 9));
	draw_group_labeled_text(12 + y_off, 39, "Entire scope", 'e');

	// Buttons
	auto draw_btn = [&](int by, int bx, const std::string& btext, char bhot, bool focused) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + by, x_ + bx + btext.length(), "▄");
		for (size_t i = 0; i < btext.length(); ++i) mvaddstr(y_ + by + 1, x_ + bx + 1 + i, "▀");
		move(y_ + by, x_ + bx);
		attrset(focused ? COLOR_PAIR(14) : COLOR_PAIR(10));
		for (size_t i = 0; i < btext.length(); ++i) {
			if (std::tolower(btext[i]) == std::tolower(bhot)) {
				attrset(COLOR_PAIR(15)); addch(btext[i]); attrset(focused ? COLOR_PAIR(14) : COLOR_PAIR(10));
			} else addch(btext[i]);
		}
		attrset(0);
	};

	int btn_y = 14 + y_off;
	draw_btn(btn_y, 8, "  OK  ", 'o', focus_idx_ == (is_replace_ ? 12 : 10));
	if (is_replace_) draw_btn(btn_y, 18, " Change all ", 'a', focus_idx_ == 13);
	draw_btn(btn_y, is_replace_ ? 34 : 28, " Cancel ", 'c', focus_idx_ == (is_replace_ ? 14 : 11));
	draw_btn(btn_y, is_replace_ ? 46 : 42, " Help ", 'h', false);

	attrset(0);
}

dialog_result find_dialog::handle_key(int key)
{
	if (key == 27) return dialog_result::cancelled;
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		if (focus_idx_ == (is_replace_ ? 14 : 11)) return dialog_result::cancelled;
		return dialog_result::confirmed;
	}
	
	std::vector<std::vector<int>> groups;
	if (!is_replace_) groups = {{0}, {1, 2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}};
	else groups = {{0}, {1}, {2, 3, 4, 5}, {6, 7}, {8, 9}, {10, 11}, {12, 13, 14}};

	auto get_group_of = [&](int idx) -> int {
		for (size_t i = 0; i < groups.size(); ++i) {
			for (int item : groups[i]) if (item == idx) return static_cast<int>(i);
		}
		return 0;
	};

	auto get_item_idx_in_group = [&](int idx, int g) -> int {
		for (size_t i = 0; i < groups[g].size(); ++i) if (groups[g][i] == idx) return static_cast<int>(i);
		return 0;
	};

	if (key == '\t' || key == KEY_BTAB) {
		int g = get_group_of(focus_idx_);
		if (key == '\t') g = (g + 1) % groups.size();
		else g = (g - 1 + groups.size()) % groups.size();
		focus_idx_ = groups[g][0];
		return dialog_result::pending;
	}

	if (key == KEY_DOWN || key == KEY_RIGHT || key == KEY_UP || key == KEY_LEFT) {
		int g = get_group_of(focus_idx_);
		int i = get_item_idx_in_group(focus_idx_, g);
		if (key == KEY_DOWN || key == KEY_RIGHT) i = (i + 1) % groups[g].size();
		else i = (i - 1 + groups[g].size()) % groups[g].size();
		focus_idx_ = groups[g][i];
		return dialog_result::pending;
	}

	if (focus_idx_ == 0 || (is_replace_ && focus_idx_ == 1)) {
		std::string& buf = (focus_idx_ == 0) ? params_.query : params_.replacement;
		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!buf.empty()) buf.pop_back();
		} else if (key >= 32 && key <= 126) {
			buf += static_cast<char>(key);
		}
	} else if (key == ' ') {
		if (!is_replace_) {
			switch (focus_idx_) {
				case 1: params_.ignore_case = !params_.ignore_case; break;
				case 2: params_.whole_words = !params_.whole_words; break;
				case 3: params_.regex = !params_.regex; break;
				case 4: params_.backward = false; break;
				case 5: params_.backward = true; break;
				case 6: params_.selected_text_only = false; break;
				case 7: params_.selected_text_only = true; break;
				case 8: params_.from_cursor = true; break;
				case 9: params_.from_cursor = false; break;
				case 10: return dialog_result::confirmed;
				case 11: return dialog_result::cancelled;
			}
		} else {
			switch (focus_idx_) {
				case 2: params_.ignore_case = !params_.ignore_case; break;
				case 3: params_.whole_words = !params_.whole_words; break;
				case 4: params_.regex = !params_.regex; break;
				case 5: params_.prompt_on_replace = !params_.prompt_on_replace; break;
				case 6: params_.backward = false; break;
				case 7: params_.backward = true; break;
				case 8: params_.selected_text_only = false; break;
				case 9: params_.selected_text_only = true; break;
				case 10: params_.from_cursor = true; break;
				case 11: params_.from_cursor = false; break;
				case 12: return dialog_result::confirmed;
				case 13: return dialog_result::confirmed; 
				case 14: return dialog_result::cancelled;
			}
		}
	}
	
	return dialog_result::pending;
}

search_params find_dialog::get_search_params() const
{
	return params_;
}
