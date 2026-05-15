#include "dialog.h"
#include "event_logger.h"
#include <ncurses.h>
#include <algorithm>

dialog::dialog(const std::string& title, int width, int height)
	: title_(title), width_(width), height_(height)
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

	// Double line border with A_BOLD
	attron(A_BOLD);
	mvaddstr(y_, x_, "╔");
	for (int i = 1; i < width_ - 1; ++i) addstr("═");
	addstr("╗");

	for (int i = 1; i < height_ - 1; ++i) {
		mvaddstr(y_ + i, x_, "║");
		mvaddstr(y_ + i, x_ + width_ - 1, "║");
	}

	mvaddstr(y_ + height_ - 1, x_, "╚");
	for (int i = 1; i < width_ - 1; ++i) addstr("═");
	addstr("╝");

	// Title
	if (!title_.empty()) {
		std::string displayed_title = " " + title_ + " ";
		int title_x = x_ + (width_ - displayed_title.length()) / 2;
		mvaddstr(y_, title_x, displayed_title.c_str());
	}
	attroff(A_BOLD);
	attroff(COLOR_PAIR(1));
}

input_dialog::input_dialog(const std::string& title, const std::string& prompt, const std::string& initial_value)
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
	attron(A_REVERSE);
	move(y_ + 4, x_ + 2);
	for (int i = 0; i < width_ - 4; ++i) addch(' ');
	mvaddstr(y_ + 4, x_ + 2, buffer_.c_str());
	attroff(A_REVERSE);
	
	attroff(COLOR_PAIR(1));
}

dialog_result input_dialog::handle_key(int key)
{
	event_logger::get_instance().log("Dialog handling key: " + std::to_string(key));
	if (key == 27) return dialog_result::cancelled;
	if (key == 10 || key == 13 || key == KEY_ENTER) return dialog_result::confirmed;
	
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

message_dialog::message_dialog(const std::string& title, const std::vector<std::string>& lines)
	: dialog(title, 40, static_cast<int>(lines.size()) + 6), lines_(lines)
{
	// Adjust width if any line is too long
	for (const auto& line : lines) {
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
	int btn_y = y_ + height_ - 3; // Move up one line to avoid clobbering border

	// Button Shadow
	// Side shadow (full block)
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
