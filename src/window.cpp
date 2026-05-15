#include "window.h"
#include "event_logger.h"
#include <ncurses.h>

window::window(int id, int x, int y, int width, int height, const std::string& title)
	: id_(id), x_(x), y_(y), width_(width), height_(height), title_(title)
{
}

void window::set_active(bool active)
{
	is_active_ = active;
}

bool window::is_active() const
{
	return is_active_;
}

event_queue& window::get_queue()
{
	return window_queue_;
}

void window::attach_document(std::shared_ptr<document> doc)
{
	doc_ = doc;
	// Update title to document filename if empty
	if (doc_ && title_.empty()) {
		title_ = doc_->get_filename();
	}
}

void window::set_cursor_position() const
{
	if (doc_) {
		auto l = doc_->get_line(doc_->get_cursor_y());
		int display_col = l ? l->char_to_display_col(doc_->get_cursor_x()) : 0;
		int screen_y = y_ + 1 + doc_->get_cursor_y() - top_line_;
		int screen_x = x_ + 1 + display_col - left_column_;
		move(screen_y, screen_x);
	}
}

bool window::process_events()
{
	bool needs_render = false;
	while (auto ev = window_queue_.pop()) {
		event_logger::get_instance().log("Window " + std::to_string(id_) + " processing key: " + std::to_string(ev->key_code));
		if (ev->type == event_type::key_press && doc_) {
			if (ev->key_code == KEY_UP) {
				doc_->move_cursor(0, -1);
				needs_render = true;
			} else if (ev->key_code == KEY_DOWN) {
				doc_->move_cursor(0, 1);
				event_logger::get_instance().log("Cursor moved to: " + std::to_string(doc_->get_cursor_y()));
				needs_render = true;
			} else if (ev->key_code == KEY_LEFT) {
				doc_->move_cursor(-1, 0);
				needs_render = true;
			} else if (ev->key_code == KEY_RIGHT) {
				doc_->move_cursor(1, 0);
				needs_render = true;
			} else if (!ev->utf8_char.empty() && (ev->key_code >= 32 || ev->key_code == 9) && ev->key_code != 127) {
				doc_->insert_char(ev->utf8_char);
				needs_render = true;
			} else if (ev->key_code == KEY_BACKSPACE || ev->key_code == 127 || ev->key_code == 8) {
				doc_->backspace();
				needs_render = true;
			} else if (ev->key_code == 13 || ev->key_code == KEY_ENTER) {
				doc_->split_line();
				needs_render = true;
			} else if (ev->key_code == 10) { // Ctrl-J
				doc_->delete_to_eol();
				needs_render = true;
			} else if (ev->key_code == -111 || ev->key_code == -79) { // Alt-O
				doc_->delete_to_bol();
				needs_render = true;
			} else if (ev->key_code == 25) { // Ctrl-Y
				doc_->delete_line();
				needs_render = true;
			} else if (ev->key_code == 1) { // Ctrl-A
				doc_->move_to_bol();
				needs_render = true;
			} else if (ev->key_code == 5) { // Ctrl-E
				doc_->move_to_eol();
				needs_render = true;
			} else if (ev->key_code == 4) { // Ctrl-D
				doc_->delete_char();
				needs_render = true;
			} else if (ev->key_code == 21) { // Ctrl-U
				doc_->move_page_up(get_content_height());
				needs_render = true;
			} else if (ev->key_code == 22) { // Ctrl-V
				doc_->move_page_down(get_content_height());
				needs_render = true;
			} else if (ev->key_code == 24) { // Ctrl-X
				doc_->move_next_word();
				needs_render = true;
			} else if (ev->key_code == 26) { // Ctrl-Z
				doc_->move_prev_word();
				needs_render = true;
			} else if (ev->key_code == 23) { // Ctrl-W
				doc_->delete_word_forward();
				needs_render = true;
			} else if (ev->key_code == 15) { // Ctrl-O
				doc_->delete_word_backward();
				needs_render = true;
			}
		}
	}
	
	return needs_render;
}

int window::get_cursor_x() const
{
	if (!doc_) return -1;
	auto l = doc_->get_line(doc_->get_cursor_y());
	return l ? l->char_to_display_col(doc_->get_cursor_x()) : 0;
}

int window::get_cursor_y() const
{
	return doc_ ? doc_->get_cursor_y() : -1;
}

void window::draw() const
{
	update_viewport();
	draw_content();
	draw_border();
}

void window::update_viewport() const
{
	if (!doc_) return;
	
	auto l = doc_->get_line(doc_->get_cursor_y());
	int display_col = l ? l->char_to_display_col(doc_->get_cursor_x()) : 0;
	int cy = doc_->get_cursor_y();
	
	if (cy < top_line_) {
		top_line_ = cy;
	} else if (cy >= top_line_ + height_ - 2) {
		top_line_ = cy - (height_ - 2) + 1;
	}
	
	if (display_col < left_column_) {
		left_column_ = display_col;
	} else if (display_col >= left_column_ + width_ - 2) {
		left_column_ = display_col - (width_ - 2) + 1;
	}
}

void window::draw_content() const
{
	int sel_start_x, sel_start_y, sel_end_x, sel_end_y;
	bool has_sel = false;
	if (doc_ && doc_->has_selection()) {
		doc_->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
		has_sel = true;
	}

	for (int i = 1; i < height_ - 1; ++i) {
		int doc_line_idx = top_line_ + i - 1;
		move(y_ + i, x_ + 1);
		
		// Clear line background
		attrset(COLOR_PAIR(3));
		for (int k = 0; k < width_ - 2; ++k) addch(' ');

		if (!doc_ || doc_line_idx >= static_cast<int>(doc_->get_line_count())) {
			continue;
		}

		auto current_l = doc_->get_line(doc_line_idx);
		if (!current_l) continue;
		
		std::string line_text = current_l->get_text();
		size_t char_count = current_l->length_in_chars();
		int current_display_col = 0;

		for (size_t char_idx = 0; char_idx < char_count; ++char_idx) {
			int start_col = current_display_col;
			
			// Determine character width and content
			std::string utf8_char;
			size_t byte_off = current_l->char_to_byte_offset(char_idx);
			size_t next_byte_off = current_l->char_to_byte_offset(char_idx + 1);
			utf8_char = line_text.substr(byte_off, next_byte_off - byte_off);
			
			int char_width = 1;
			if (utf8_char == "\t") {
				char_width = 8 - (start_col % 8);
			}
			
			int end_col = start_col + char_width;
			current_display_col = end_col;

			// Check if any part of character is within horizontal viewport
			for (int col = start_col; col < end_col; ++col) {
				if (col >= left_column_ && col < left_column_ + width_ - 2) {
					int screen_x_offset = col - left_column_;
					move(y_ + i, x_ + 1 + screen_x_offset);

					bool in_selection = false;
					if (has_sel) {
						if (doc_line_idx > sel_start_y && doc_line_idx < sel_end_y) {
							in_selection = true;
						} else if (doc_line_idx == sel_start_y && doc_line_idx == sel_end_y) {
							in_selection = (static_cast<int>(char_idx) >= sel_start_x && static_cast<int>(char_idx) < sel_end_x);
						} else if (doc_line_idx == sel_start_y) {
							in_selection = (static_cast<int>(char_idx) >= sel_start_x);
						} else if (doc_line_idx == sel_end_y) {
							in_selection = (static_cast<int>(char_idx) < sel_end_x);
						}
					}

					syntax_attribute attr = current_l->get_attribute(char_idx);
					int pair = 3;
					if (in_selection) {
						pair = 8;
						if (attr == syntax_attribute::keyword) pair = 13;
					} else {
						if (attr == syntax_attribute::keyword) pair = 12;
					}

					attrset(COLOR_PAIR(pair));
					if (utf8_char == "\t") {
						addch(' ');
					} else {
						addstr(utf8_char.c_str());
					}
				}
			}
		}
	}
	attrset(0);
}

void window::draw_border() const
{
	attrset(COLOR_PAIR(5));

	std::string current_title = title_;
	if (doc_) {
		current_title = doc_->get_filename();
		if (current_title.empty()) current_title = "untitled";
		size_t last_slash = current_title.find_last_of("/\\");
		if (last_slash != std::string::npos) {
			current_title = current_title.substr(last_slash + 1);
		}
	}

	// Draw top and bottom borders
	for (int i = 1; i < width_ - 1; ++i) {
		mvaddstr(y_, x_ + i, "═");
		mvaddstr(y_ + height_ - 1, x_ + i, "═");
	}

	// Draw left and right borders
	for (int i = 1; i < height_ - 1; ++i) {
		mvaddstr(y_ + i, x_, "║");
		mvaddstr(y_ + i, x_ + width_ - 1, "║");
	}

	// Draw corners
	mvaddstr(y_, x_, "╔");
	mvaddstr(y_, x_ + width_ - 1, "╗");
	mvaddstr(y_ + height_ - 1, x_, "╚");
	mvaddstr(y_ + height_ - 1, x_ + width_ - 1, "╝");

	// Draw title
	if (!current_title.empty()) {
		int title_x = x_ + (width_ - current_title.length()) / 2;
		mvprintw(y_, title_x - 1, " %s ", current_title.c_str());
	}

	// Draw close widget
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(3)); // Bright Yellow
	addstr("■");
	attroff(COLOR_PAIR(3));
	attron(COLOR_PAIR(5));
	addstr("]");

	// Draw window number
	mvprintw(y_, x_ + width_ - 6, "=%d=", id_);

	attroff(COLOR_PAIR(5));
	// Draw scrollbars
	attron(COLOR_PAIR(4));
	for (int i = 1; i < height_ - 1; ++i) {
		mvaddstr(y_ + i, x_ + width_ - 1, "▒");
	}
	for (int i = 1; i < width_ - 1; ++i) {
		mvaddstr(y_ + height_ - 1, x_ + i, "▒");
	}
	
	// Scroll arrows
	mvaddstr(y_ + 1, x_ + width_ - 1, "▲");
	mvaddstr(y_ + height_ - 2, x_ + width_ - 1, "▼");
	mvaddstr(y_ + height_ - 1, x_ + 1, "◄");
	mvaddstr(y_ + height_ - 1, x_ + width_ - 2, "►");
	attroff(COLOR_PAIR(4));
}
