#include "ui/dialog.h"
#include <algorithm>
#include <ncurses.h>
#include "event_logger.h"

legacy_ui_button::legacy_ui_button(const std::string &t, char hk, const std::string &res, dialog_result act, int px, int py)
    : text(t), hotkey(hk), result_string(res), action(act), x(px), y(py), width(static_cast<int>(t.length()))
{
}

void legacy_ui_button::draw() const
{
	attron(COLOR_PAIR(1));
	mvaddstr(y, x + text.length(), "▄");
	std::string shadow_str;
	for (size_t j = 0; j < text.length(); ++j) shadow_str += "▀";
	mvaddstr(y + 1, x + 1, shadow_str.c_str());
	
	if (is_focused) attrset(COLOR_PAIR(10));
	else attrset(COLOR_PAIR(8));
	
	mvaddstr(y, x, text.c_str());
	
	if (hotkey != '\0') {
		size_t hk_pos = text.find(hotkey);
		if (hk_pos != std::string::npos) {
			if (is_focused) attron(COLOR_PAIR(12));
			else attron(COLOR_PAIR(11));
			mvaddch(y, x + hk_pos, text[hk_pos]);
		}
	}
	attrset(COLOR_PAIR(1));
}

dialog::dialog(const std::string &title, int width, int height) : title_(title), width_(width), height_(height)
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	x_ = (max_x - width_) / 2;
	y_ = (max_y - height_) / 2;
}

void dialog::draw() const
{
	// Draw shadow
	attron(COLOR_PAIR(6));
	for (int i = 0; i < height_; ++i) {
		mvaddstr(y_ + i + 1, x_ + width_, "  ");
	}
	for (int i = 0; i < width_; ++i) {
		mvaddstr(y_ + height_, x_ + i + 2, " ");
	}
	attroff(COLOR_PAIR(6));

	// Draw box
	attron(COLOR_PAIR(1));
	for (int i = 0; i < height_; ++i) {
		move(y_ + i, x_);
		for (int j = 0; j < width_; ++j) {
			addch(' ');
		}
	}

	// Double line border with Pair 11
	attron(COLOR_PAIR(11));
	mvaddstr(y_, x_, "╔");
	for (int i = 1; i < width_ - 1; ++i)
		addstr("═");
	addstr("╗");

	for (int i = 1; i < height_ - 1; ++i) {
		mvaddstr(y_ + i, x_, "║");
		mvaddstr(y_ + i, x_ + width_ - 1, "║");
	}

	mvaddstr(y_ + height_ - 1, x_, "╚");
	for (int i = 1; i < width_ - 1; ++i)
		addstr("═");
	addstr("╝");
	
	// Close button [■]
	attron(COLOR_PAIR(14)); // Black on Green
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(14) | A_BOLD);
	addstr("■");
	attron(COLOR_PAIR(14));
	addstr("]");
	attroff(COLOR_PAIR(14));
	
	attroff(COLOR_PAIR(11));

	// Title
	if (!title_.empty()) {
		attron(COLOR_PAIR(1));
		std::string displayed_title = " " + title_ + " ";
		int title_x = x_ + (width_ - displayed_title.length()) / 2;
		mvaddstr(y_, title_x, displayed_title.c_str());
		attroff(COLOR_PAIR(1));
	}
	attroff(COLOR_PAIR(1));
}

void dialog::draw_buttons(int focused_button_idx) const
{
	for (size_t i = 0; i < buttons_.size(); ++i) {
		buttons_[i].set_focus(static_cast<int>(i) == focused_button_idx);
		buttons_[i].draw();
	}
}

input_dialog::input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value)
    : dialog(title, 50, 7), prompt_(prompt), buffer_(initial_value)
{
}

void input_dialog::draw() const
{
	dialog::draw();
	attron(COLOR_PAIR(1));

	// Prompt
	mvaddstr(y_ + 2, x_ + 2, prompt_.c_str());

	// Input field box
	attron(COLOR_PAIR(5));
	move(y_ + 4, x_ + 2);
	for (int i = 0; i < width_ - 4; ++i)
		addch(' ');
	mvaddstr(y_ + 4, x_ + 2, buffer_.c_str());
	attroff(COLOR_PAIR(5));

	attroff(COLOR_PAIR(1));
}

dialog_result input_dialog::handle_key(int key)
{
	event_logger::get_instance().log("Dialog handling key: " + std::to_string(key));
	if (key == 27)
		return dialog_result::cancelled;
	if (key == 10 || key == 13 || key == KEY_ENTER)
		return dialog_result::confirmed;

	if (key == KEY_BACKSPACE || key == 127 || key == 8) {
		if (!buffer_.empty()) {
			buffer_.pop_back();
		}
		return dialog_result::pending;
	}

	if (key >= 32 && key <= 126) {
		buffer_ += static_cast<char>(key);
		return dialog_result::pending;
	}

	return dialog_result::pending;
}

std::string input_dialog::get_result() const
{
	return buffer_;
}

message_dialog::message_dialog(const std::string &title, const std::vector<std::string> &lines)
    : dialog(title, 40, static_cast<int>(lines.size()) + 6), lines_(lines)
{
	// Adjust width if any line is too long
	for (const auto &line : lines) {
		if (static_cast<int>(line.length()) + 6 > width_) {
			width_ = static_cast<int>(line.length()) + 6;
		}
	}
	// Recalculate x/y after width adjustment
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	x_ = (max_x - width_) / 2;
	y_ = (max_y - height_) / 2;
}

void message_dialog::draw() const
{
	dialog::draw();
	attron(COLOR_PAIR(1));

	for (size_t i = 0; i < lines_.size(); ++i) {
		int text_x = x_ + (width_ - static_cast<int>(lines_[i].length())) / 2;
		mvaddstr(y_ + 2 + i, text_x, lines_[i].c_str());
	}

	// OK Button
	std::string ok_text = "  OK  ";
	int btn_x = x_ + (width_ - static_cast<int>(ok_text.length())) / 2;
	int btn_y = y_ + height_ - 3;

	// Button Shadow
	// Side shadow (half block)
	attron(COLOR_PAIR(1));
	mvaddstr(btn_y, btn_x + static_cast<int>(ok_text.length()), "▄");
	attroff(COLOR_PAIR(1));

	// Bottom shadow (half block)
	attron(COLOR_PAIR(1));
	mvaddstr(btn_y + 1, btn_x + 1, "▀▀▀▀▀▀");
	attroff(COLOR_PAIR(1));

	// Button surface
	attron(COLOR_PAIR(10));
	mvaddstr(btn_y, btn_x, ok_text.c_str());
	attroff(COLOR_PAIR(10));

	attroff(COLOR_PAIR(1));
}

dialog_result message_dialog::handle_key(int key)
{
	// Any of these keys close the dialog
	if (key == 27 || key == 10 || key == 13 || key == KEY_ENTER || key == 32) {
		return dialog_result::confirmed;
	}
	return dialog_result::pending;
}

save_prompt_dialog::save_prompt_dialog(const std::string &filename)
    : dialog("Unsaved Changes", 50, 8), filename_(filename)
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	x_ = (max_x - width_) / 2;
	y_ = (max_y - height_) / 2;

	int by = y_ + height_ - 3;
	buttons_.emplace_back("  Save  ", 'S', "save", dialog_result::confirmed, x_ + 4, by);
	buttons_.emplace_back(" Discard ", 'D', "discard", dialog_result::confirmed, x_ + 18, by);
	buttons_.emplace_back(" Cancel ", 'C', "cancel", dialog_result::cancelled, x_ + 34, by);
}

void save_prompt_dialog::draw() const
{
	dialog::draw();
	attron(COLOR_PAIR(1));

	std::string msg = "Save changes to " + filename_ + "?";
	int text_x = x_ + (width_ - static_cast<int>(msg.length())) / 2;
	mvaddstr(y_ + 2, text_x, msg.c_str());

	draw_buttons(focus_idx_);
}

std::optional<dialog_result> save_prompt_dialog::handle_mouse(int mouse_x, int mouse_y)
{
	for (size_t i = 0; i < buttons_.size(); ++i) {
		if (buttons_[i].contains(mouse_x, mouse_y)) {
			focus_idx_ = i;
			return buttons_[i].action;
		}
	}
	return std::nullopt;
}

dialog_result save_prompt_dialog::handle_key(int key)
{
	if (key == 27) { // ESC
		return dialog_result::cancelled;
	} else if (key == KEY_LEFT) {
		focus_idx_ = (focus_idx_ - 1 + 3) % 3;
	} else if (key == KEY_RIGHT) {
		focus_idx_ = (focus_idx_ + 1) % 3;
	} else if (key == '\n' || key == 13 || key == KEY_ENTER) {
		if (focus_idx_ == 2) return dialog_result::cancelled;
		return dialog_result::confirmed;
	} else {
		char c = std::tolower(static_cast<char>(key));
		if (c == 's') { focus_idx_ = 0; return dialog_result::confirmed; }
		if (c == 'd') { focus_idx_ = 1; return dialog_result::confirmed; }
		if (c == 'c') { focus_idx_ = 2; return dialog_result::cancelled; }
	}
	return dialog_result::pending;
}

std::string save_prompt_dialog::get_result() const
{
	if (focus_idx_ == 0) return "save";
	if (focus_idx_ == 1) return "discard";
	return "cancel";
}

force_quit_dialog::force_quit_dialog()
    : dialog("Force Quit", 50, 9)
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	x_ = (max_x - width_) / 2;
	y_ = (max_y - height_) / 2;
	start_time_ = std::chrono::steady_clock::now();

	int by = y_ + height_ - 3;
	buttons_.emplace_back("  Exit  ", 'E', "exit", dialog_result::confirmed, x_ + 4, by);
	buttons_.emplace_back(" Save All ", 'S', "save_all", dialog_result::confirmed, x_ + 16, by);
	buttons_.emplace_back(" Cancel ", 'C', "cancel", dialog_result::cancelled, x_ + 32, by);
}

bool force_quit_dialog::tick()
{
	if (countdown_active_) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
		int new_remaining = 5 - static_cast<int>(elapsed);
		if (new_remaining <= 0) {
			return true; // Expired
		}
		remaining_seconds_ = new_remaining;
	}
	return false;
}

void force_quit_dialog::draw() const
{
	dialog::draw();
	attron(COLOR_PAIR(1));

	std::string msg = "Unsaved changes! Quit anyway?";
	int text_x = x_ + (width_ - static_cast<int>(msg.length())) / 2;
	mvaddstr(y_ + 2, text_x, msg.c_str());

	if (countdown_active_) {
		std::string count_msg = "(Auto-closing in " + std::to_string(remaining_seconds_) + "s)";
		int count_x = x_ + (width_ - static_cast<int>(count_msg.length())) / 2;
		mvaddstr(y_ + 4, count_x, count_msg.c_str());
	}

	draw_buttons(focus_idx_);
}

std::optional<dialog_result> force_quit_dialog::handle_mouse(int mouse_x, int mouse_y)
{
	for (size_t i = 0; i < buttons_.size(); ++i) {
		if (buttons_[i].contains(mouse_x, mouse_y)) {
			focus_idx_ = i;
			return buttons_[i].action;
		}
	}
	return std::nullopt;
}

dialog_result force_quit_dialog::handle_key(int key)
{
	countdown_active_ = false; // Any key press stops the countdown
	if (key == 27) { // ESC instantly exits per user request
		focus_idx_ = 0; // Exit
		return dialog_result::confirmed;
	} else if (key == KEY_LEFT) {
		focus_idx_ = (focus_idx_ - 1 + 3) % 3;
	} else if (key == KEY_RIGHT) {
		focus_idx_ = (focus_idx_ + 1) % 3;
	} else if (key == '\n' || key == 13 || key == KEY_ENTER) {
		if (focus_idx_ == 2) return dialog_result::cancelled;
		return dialog_result::confirmed;
	} else {
		char c = std::tolower(static_cast<char>(key));
		if (c == 'e' || c == 'x') { focus_idx_ = 0; return dialog_result::confirmed; }
		if (c == 's') { focus_idx_ = 1; return dialog_result::confirmed; }
		if (c == 'c') { focus_idx_ = 2; return dialog_result::cancelled; }
	}
	return dialog_result::pending;
}

std::string force_quit_dialog::get_result() const
{
	if (focus_idx_ == 0) return "exit";
	if (focus_idx_ == 1) return "save_all";
	return "cancel";
}

ask_user_dialog::ask_user_dialog(const std::string& question, const std::vector<std::string>& options)
    : dialog("Question", std::max<int>(60, question.length() + 6), 9 + options.size()), question_(question), options_(options)
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	x_ = (max_x - width_) / 2;
	y_ = (max_y - height_) / 2;

	int current_y = y_ + 4 + options_.size() + 2;
	int btn_x_center = width_ / 2;
	buttons_.emplace_back("   OK   ", 'O', "ok", dialog_result::confirmed, x_ + btn_x_center - 12, current_y);
	buttons_.emplace_back(" Cancel ", 'C', "cancel", dialog_result::cancelled, x_ + btn_x_center + 2, current_y);
}

void ask_user_dialog::draw() const
{
	dialog::draw();
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 2, x_ + 3, question_.c_str());

	int current_y = y_ + 4;
	int available_width = width_ - 7; // Matches the width block of the text field (which goes up to width_ - 4, minus starting pos 3)
	for (size_t i = 0; i < options_.size(); ++i) {
		bool focused = (focus_idx_ == static_cast<int>(i));
		move(current_y, x_ + 3);
		if (focused) attrset(COLOR_PAIR(19));
		else attrset(COLOR_PAIR(17));
		addstr(focused ? "(•) " : "( ) ");
		
		std::string opt_text = options_[i];
		if (opt_text.length() > static_cast<size_t>(available_width - 4)) {
			opt_text = opt_text.substr(0, available_width - 7) + "...";
		}
		
		addstr(opt_text.c_str());
		
		// Pad remaining width with spaces
		int pad_len = available_width - 4 - opt_text.length();
		for (int p = 0; p < pad_len; ++p) {
			addch(' ');
		}
		current_y++;
	}

	bool text_focused = (focus_idx_ == static_cast<int>(options_.size()));
	attrset(COLOR_PAIR(1));
	mvaddstr(current_y, x_ + 3, "Other:");
	attrset(COLOR_PAIR(3));
	move(current_y, x_ + 10);
	for (int i = 0; i < width_ - 14; ++i) addch(' ');
	mvaddstr(current_y, x_ + 10, custom_answer_.c_str());
	if (text_focused) {
		move(current_y, x_ + 10 + custom_answer_.length());
		attrset(COLOR_PAIR(19));
		addch(' ');
		attrset(COLOR_PAIR(3));
	}

	int btn_focus = -1;
	if (focus_idx_ == static_cast<int>(options_.size() + 1)) btn_focus = 0;
	if (focus_idx_ == static_cast<int>(options_.size() + 2)) btn_focus = 1;
	draw_buttons(btn_focus);
}

std::optional<dialog_result> ask_user_dialog::handle_mouse(int mouse_x, int mouse_y)
{
	for (size_t i = 0; i < buttons_.size(); ++i) {
		if (buttons_[i].contains(mouse_x, mouse_y)) {
			focus_idx_ = options_.size() + 1 + i;
			return buttons_[i].action;
		}
	}
	return std::nullopt;
}

dialog_result ask_user_dialog::handle_key(int key)
{
	int total_items = options_.size() + 3;
	
	if (key == 27) { // ESC
		return dialog_result::cancelled;
	} else if (key == KEY_DOWN || key == '\t') {
		focus_idx_ = (focus_idx_ + 1) % total_items;
		return dialog_result::pending;
	} else if (key == KEY_UP || key == KEY_BTAB) {
		focus_idx_ = (focus_idx_ - 1 + total_items) % total_items;
		return dialog_result::pending;
	}
	
	if (key == '\n' || key == 13 || key == KEY_ENTER) {
		if (focus_idx_ == static_cast<int>(options_.size() + 2)) {
			return dialog_result::cancelled;
		} else {
			return dialog_result::confirmed;
		}
	}

	if (focus_idx_ == static_cast<int>(options_.size())) {
		// Text box is focused
		if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!custom_answer_.empty()) custom_answer_.pop_back();
		} else if (key >= 32 && key <= 126) {
			custom_answer_ += static_cast<char>(key);
		}
	}
	return dialog_result::pending;
}

std::string ask_user_dialog::get_result() const
{
	if (focus_idx_ < static_cast<int>(options_.size())) {
		return options_[focus_idx_];
	} else {
		return custom_answer_;
	}
}
