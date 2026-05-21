#include "ui/find_dialog.h"
#include <cctype>
#include <ncurses.h>

find_dialog_legacy::find_dialog_legacy(const std::string &title, const search_params &initial_params, bool is_replace)
    : dialog(title, 64, get_height(is_replace)), params_(initial_params), is_replace_(is_replace)
{
}

int find_dialog_legacy::get_height(bool is_replace)
{
	if (is_replace)
		return 18;
	return 16;
}

void find_dialog_legacy::draw_group_box(int gy, int gx, int gw, int gh, const std::string &gtitle) const
{
	// Fill background
	attrset(COLOR_PAIR(17));
	for (int i = 1; i < gh; ++i) {
		move(y_ + gy + i, x_ + gx);
		for (int j = 0; j < gw; ++j)
			addch(' ');
	}

	if (!gtitle.empty()) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + gy, x_ + gx, gtitle.c_str());
	}
	attrset(0);
}

void find_dialog_legacy::draw_labeled_text(int ly, int lx, const std::string &text, char hotkey) const
{
	std::string lower_text = text;
	for (char &c : lower_text)
		c = std::tolower(c);
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

void find_dialog_legacy::draw_group_labeled_text(int ly, int lx, const std::string &text, char hotkey) const
{
	std::string lower_text = text;
	for (char &c : lower_text)
		c = std::tolower(c);
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

void find_dialog_legacy::draw() const
{
	dialog::draw();
	attrset(COLOR_PAIR(1));

	int y_off;
	if (is_replace_) {
		y_off = 2;
	} else {
		y_off = 0;
	}

	// Label: Text to find
	draw_labeled_text(2, 2, "Text to find", 't');

	// Input field 1
	attrset(COLOR_PAIR(3));
	move(y_ + 2, x_ + 16);
	for (int i = 0; i < 40; ++i)
		addch(' ');
	mvaddstr(y_ + 2, x_ + 16, params_.query.c_str());
	if (focus_idx_ == focus_item::query) {
		move(y_ + 2, x_ + 16 + params_.query.length());
		attrset(COLOR_PAIR(19));
		addch(' ');
		attrset(COLOR_PAIR(3));
	}
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 2, x_ + 56, "[↓]");

	if (is_replace_) {
		draw_labeled_text(4, 2, "Replace with", 'n');
		attrset(COLOR_PAIR(3));
		move(y_ + 4, x_ + 16);
		for (int i = 0; i < 40; ++i)
			addch(' ');
		mvaddstr(y_ + 4, x_ + 16, params_.replacement.c_str());
		if (focus_idx_ == focus_item::replacement) {
			move(y_ + 4, x_ + 16 + params_.replacement.length());
			attrset(COLOR_PAIR(19));
			addch(' ');
			attrset(COLOR_PAIR(3));
		}
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + 4, x_ + 56, "[↓]");
	}

	// Groups
	int h;
	if (is_replace_) {
		h = 5;
	} else {
		h = 4;
	}
	draw_group_box(5 + y_off, 2, 30, h, "Options");
	draw_group_box(5 + y_off, 33, 28, 3, "Direction");
	draw_group_box(10 + y_off, 2, 30, 3, "Scope");
	draw_group_box(10 + y_off, 33, 28, 3, "Origin");

	// Focus indicator helpers
	auto draw_checkbox = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused)
			attrset(COLOR_PAIR(19));
		else
			attrset(COLOR_PAIR(17));
		addch('[');
		if (val) {
			if (focused)
				attrset(COLOR_PAIR(19));
			else
				attrset(COLOR_PAIR(17));
			addch('X');
			if (focused) {
				attrset(COLOR_PAIR(19));
			} else {
				attrset(COLOR_PAIR(17));
			}
		} else
			addch(' ');
		addch(']');
		attrset(0);
	};

	auto draw_radio = [&](int ry, int rx, bool val, bool focused) {
		move(y_ + ry, x_ + rx);
		if (focused)
			attrset(COLOR_PAIR(19));
		else
			attrset(COLOR_PAIR(17));
		addch('(');
		if (val) {
			if (focused)
				attrset(COLOR_PAIR(19));
			else
				attrset(COLOR_PAIR(17));
			addstr("•");
			if (focused) {
				attrset(COLOR_PAIR(19));
			} else {
				attrset(COLOR_PAIR(17));
			}
		} else
			addch(' ');
		addch(')');
		attrset(0);
	};

	

	// Options content
	draw_checkbox(6 + y_off, 4, !params_.ignore_case, focus_idx_ == focus_item::case_sensitive);
	draw_group_labeled_text(6 + y_off, 8, "Case sensitive", 'c');
	draw_checkbox(7 + y_off, 4, params_.whole_words, focus_idx_ == focus_item::whole_words);
	draw_group_labeled_text(7 + y_off, 8, "Whole words only", 'w');
	draw_checkbox(8 + y_off, 4, params_.regex, focus_idx_ == focus_item::regex);
	draw_group_labeled_text(8 + y_off, 8, "Regular expression", 'r');
	if (is_replace_) {
		draw_checkbox(9 + y_off, 4, params_.prompt_on_replace, focus_idx_ == focus_item::prompt_replace);
		draw_group_labeled_text(9 + y_off, 8, "Prompt on replace", 'p');
	}

	// Direction content
	
	draw_radio(6 + y_off, 35, !params_.backward, focus_idx_ == focus_item::dir_forward);
	draw_group_labeled_text(6 + y_off, 39, "Forward", 'f');
	if (is_replace_) {
		draw_radio(7 + y_off, 35, params_.backward, focus_idx_ == focus_item::dir_backward);
	} else {
		draw_radio(7 + y_off, 35, params_.backward, focus_idx_ == focus_item::prompt_replace);
	}
	draw_group_labeled_text(7 + y_off, 39, "Backward", 'b');

	// Scope content
	
	draw_radio(11 + y_off, 4, !params_.selected_text_only, focus_idx_ == focus_item::scope_global);
	draw_group_labeled_text(11 + y_off, 8, "Global", 'g');
	
	draw_radio(12 + y_off, 4, params_.selected_text_only, focus_idx_ == focus_item::scope_selected);
	draw_group_labeled_text(12 + y_off, 8, "Selected text", 's');

	// Origin content
	
	draw_radio(11 + y_off, 35, params_.from_cursor, focus_idx_ == focus_item::origin_cursor);
	draw_group_labeled_text(11 + y_off, 39, "From cursor", 'o');
	
	draw_radio(12 + y_off, 35, !params_.from_cursor, focus_idx_ == focus_item::origin_entire);
	draw_group_labeled_text(12 + y_off, 39, "Entire scope", 'e');

	// Buttons
	auto draw_btn = [&](int by, int bx, const std::string &btext, char bhot, bool focused) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + by, x_ + bx + btext.length(), "▄");
		for (size_t i = 0; i < btext.length(); ++i)
			mvaddstr(y_ + by + 1, x_ + bx + 1 + i, "▀");
		move(y_ + by, x_ + bx);
		if (focused) {
			attrset(COLOR_PAIR(14));
		} else {
			attrset(COLOR_PAIR(10));
		}
		for (size_t i = 0; i < btext.length(); ++i) {
			if (std::tolower(btext[i]) == std::tolower(bhot)) {
				attrset(COLOR_PAIR(15));
				addch(btext[i]);
				if (focused) {
					attrset(COLOR_PAIR(14));
				} else {
					attrset(COLOR_PAIR(10));
				}
			} else
				addch(btext[i]);
		}
		attrset(0);
	};

	int btn_y = 14 + y_off;
	
	draw_btn(btn_y, 8, "  OK  ", 'o', focus_idx_ == focus_item::btn_ok);
	if (is_replace_)
		draw_btn(btn_y, 18, " Change all ", 'a', focus_idx_ == focus_item::btn_change_all);
	int bx_pos;
	if (is_replace_) {
		bx_pos = 34;
	} else {
		bx_pos = 28;
	}
	
	draw_btn(btn_y, bx_pos, " Cancel ", 'c', focus_idx_ == focus_item::btn_cancel);
	int bx_pos2;
	if (is_replace_) {
		bx_pos2 = 46;
	} else {
		bx_pos2 = 42;
	}
	draw_btn(btn_y, bx_pos2, " Help ", 'h', false);

	attrset(0);
}

dialog_result find_dialog_legacy::handle_key(int key)
{
	if (key == 27)
		return dialog_result::cancelled;
	if (key == 13 || key == 10 || key == KEY_ENTER) {
		
		if (focus_idx_ == focus_item::btn_cancel)
			return dialog_result::cancelled;
		return dialog_result::confirmed;
	}

	std::vector<std::vector<focus_item>> groups;
	if (!is_replace_)
		groups = {{focus_item::query}, {focus_item::case_sensitive, focus_item::whole_words, focus_item::regex}, {focus_item::dir_forward, focus_item::dir_backward}, {focus_item::scope_global, focus_item::scope_selected}, {focus_item::origin_cursor, focus_item::origin_entire}, {focus_item::btn_ok, focus_item::btn_cancel}};
	else
		groups = {{focus_item::query}, {focus_item::replacement}, {focus_item::case_sensitive, focus_item::whole_words, focus_item::regex, focus_item::prompt_replace}, {focus_item::dir_forward, focus_item::dir_backward}, {focus_item::scope_global, focus_item::scope_selected}, {focus_item::origin_cursor, focus_item::origin_entire}, {focus_item::btn_ok, focus_item::btn_change_all, focus_item::btn_cancel}};

	auto get_group_of = [&](focus_item idx) -> int {
		for (size_t i = 0; i < groups.size(); ++i) {
			for (focus_item item : groups[i])
				if (item == idx)
					return static_cast<int>(i);
		}
		return 0;
	};

	auto get_item_idx_in_group = [&](focus_item idx, int g) -> int {
		for (size_t i = 0; i < groups[g].size(); ++i)
			if (groups[g][i] == idx)
				return static_cast<int>(i);
		return 0;
	};

	if (key == '\t' || key == KEY_BTAB) {
		int g = get_group_of(focus_idx_);
		if (key == '\t')
			g = (g + 1) % groups.size();
		else
			g = (g - 1 + groups.size()) % groups.size();
		focus_idx_ = groups[g][0];
		return dialog_result::pending;
	}

	if (key == KEY_DOWN || key == KEY_RIGHT || key == KEY_UP || key == KEY_LEFT) {
		int g = get_group_of(focus_idx_);
		int i = get_item_idx_in_group(focus_idx_, g);
		if (key == KEY_DOWN || key == KEY_RIGHT)
			i = (i + 1) % groups[g].size();
		else
			i = (i - 1 + groups[g].size()) % groups[g].size();
		focus_idx_ = groups[g][i];
		return dialog_result::pending;
	}

	if (focus_idx_ == focus_item::query || (is_replace_ && focus_idx_ == focus_item::replacement)) {
		std::string *buf;
		if (focus_idx_ == focus_item::query) {
			buf = &params_.query;
		} else {
			buf = &params_.replacement;
		}
		std::string &buf_ref = *buf;
		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!buf_ref.empty())
				buf_ref.pop_back();
		} else if (key >= 32 && key <= 126) {
			buf_ref += static_cast<char>(key);
		}
	
	} else if (key == ' ') {
		switch (focus_idx_) {
			case focus_item::case_sensitive:
				params_.ignore_case = !params_.ignore_case;
				break;
			case focus_item::whole_words:
				params_.whole_words = !params_.whole_words;
				break;
			case focus_item::regex:
				params_.regex = !params_.regex;
				break;
			case focus_item::prompt_replace:
				params_.prompt_on_replace = !params_.prompt_on_replace;
				break;
			case focus_item::dir_forward:
				params_.backward = false;
				break;
			case focus_item::dir_backward:
				params_.backward = true;
				break;
			case focus_item::scope_global:
				params_.selected_text_only = false;
				break;
			case focus_item::scope_selected:
				params_.selected_text_only = true;
				break;
			case focus_item::origin_cursor:
				params_.from_cursor = true;
				break;
			case focus_item::origin_entire:
				params_.from_cursor = false;
				break;
			case focus_item::btn_ok:
			case focus_item::btn_change_all:
				return dialog_result::confirmed;
			case focus_item::btn_cancel:
				return dialog_result::cancelled;
			default:
				break;
		}
	}

	return dialog_result::pending;
}

search_params find_dialog_legacy::get_search_params() const
{
	return params_;
}
