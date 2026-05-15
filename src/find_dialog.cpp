#include "find_dialog.h"
#include <ncurses.h>
#include <cctype>

find_dialog::find_dialog(const std::string& title, const search_params& initial_params)
	: dialog(title, 64, 16), params_(initial_params)
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
	
	// Label: Text to find
	draw_labeled_text(2, 2, "Text to find", 't');
	
	// Input field
	attrset(COLOR_PAIR(3)); // Yellow on Blue for input
	move(y_ + 2, x_ + 16);
	for (int i = 0; i < 40; ++i) addch(' ');
	mvaddstr(y_ + 2, x_ + 16, params_.query.c_str());
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 2, x_ + 56, "[↓]"); // History button

	// Groups
	draw_group_box(4, 2, 30, 5, "Options");
	draw_group_box(4, 33, 28, 5, "Direction");
	draw_group_box(10, 2, 30, 4, "Scope");
	draw_group_box(10, 33, 28, 4, "Origin");

	// Focus indicator
	auto draw_checkbox = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused) attron(A_REVERSE);
		else attron(COLOR_PAIR(17));
		
		addch('[');
		if (val) {
			attrset(COLOR_PAIR(8)); // Bright White on Cyan
			if (focused) attron(A_REVERSE);
			addch('X');
			attrset(COLOR_PAIR(17));
			if (focused) attron(A_REVERSE);
		} else {
			addch(' ');
		}
		addch(']');
		attrset(0);
	};

	auto draw_radio = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused) attron(A_REVERSE);
		else attron(COLOR_PAIR(17));
		
		addch('(');
		if (val) {
			attrset(COLOR_PAIR(8)); // Bright White on Cyan
			if (focused) attron(A_REVERSE);
			addstr("•");
			attrset(COLOR_PAIR(17));
			if (focused) attron(A_REVERSE);
		} else {
			addch(' ');
		}
		addch(')');
		attrset(0);
	};

	// Options content
	draw_checkbox(5, 4, !params_.ignore_case, focus_idx_ == 1);
	draw_group_labeled_text(5, 8, "Case sensitive", 'c');
	
	draw_checkbox(6, 4, params_.whole_words, focus_idx_ == 2);
	draw_group_labeled_text(6, 8, "Whole words only", 'w');
	
	draw_checkbox(7, 4, params_.regex, focus_idx_ == 3);
	draw_group_labeled_text(7, 8, "Regular expression", 'r');

	// Direction content
	draw_radio(5, 35, !params_.backward, focus_idx_ == 4);
	draw_group_labeled_text(5, 39, "Forward", 'f');
	
	draw_radio(6, 35, params_.backward, focus_idx_ == 5);
	draw_group_labeled_text(6, 39, "Backward", 'b');

	// Scope content
	draw_radio(11, 4, !params_.selected_text_only, focus_idx_ == 6);
	draw_group_labeled_text(11, 8, "Global", 'g');
	
	draw_radio(12, 4, params_.selected_text_only, focus_idx_ == 7);
	draw_group_labeled_text(12, 8, "Selected text", 's');

	// Origin content
	draw_radio(11, 35, params_.from_cursor, focus_idx_ == 8);
	draw_group_labeled_text(11, 39, "From cursor", 'o');
	
	draw_radio(12, 35, !params_.from_cursor, focus_idx_ == 9);
	draw_group_labeled_text(12, 39, "Entire scope", 'e');

	// Buttons
	auto draw_btn = [&](int by, int bx, const std::string& btext, char bhot, bool focused) {
		// Shadow
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + by, x_ + bx + btext.length(), "▄");
		for (size_t i = 0; i < btext.length(); ++i) {
			mvaddstr(y_ + by + 1, x_ + bx + 1 + i, "▀");
		}
		
		// Surface
		move(y_ + by, x_ + bx);
		if (focused) attrset(COLOR_PAIR(14)); // Black on Green
		else attrset(COLOR_PAIR(10)); // White on Green
		
		for (size_t i = 0; i < btext.length(); ++i) {
			if (std::tolower(btext[i]) == std::tolower(bhot)) {
				attrset(COLOR_PAIR(15)); // Red on Green
				addch(btext[i]);
				if (focused) attrset(COLOR_PAIR(14));
				else attrset(COLOR_PAIR(10));
			} else {
				addch(btext[i]);
			}
		}
		attrset(0);
	};

	draw_btn(14, 18, "  OK  ", 'o', focus_idx_ == 10);
	draw_btn(14, 28, " Cancel ", 'c', focus_idx_ == 11);
	draw_btn(14, 42, " Help ", 'h', false);

	attrset(0);
}

dialog_result find_dialog::handle_key(int key)
{
	if (key == 27) return dialog_result::cancelled;
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		if (focus_idx_ == 11) return dialog_result::cancelled;
		return dialog_result::confirmed;
	}
	
	if (key == '\t' || key == KEY_BTAB) {
		if (key == '\t') focus_idx_ = (focus_idx_ + 1) % 12;
		else focus_idx_ = (focus_idx_ - 1 + 12) % 12;
		return dialog_result::pending;
	}

	if (focus_idx_ == 0) {
		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!params_.query.empty()) params_.query.pop_back();
		} else if (key >= 32 && key <= 126) {
			params_.query += static_cast<char>(key);
		}
	} else if (key == ' ') {
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
	}
	
	return dialog_result::pending;
}

search_params find_dialog::get_search_params() const
{
	return params_;
}
