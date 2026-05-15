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
		int screen_y = y_ + 1 + doc_->get_cursor_y() - top_line_;
		int screen_x = x_ + 1 + doc_->get_cursor_x() - left_column_;
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
			} else if (!ev->utf8_char.empty() && ev->key_code >= 32 && ev->key_code != 127) {
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
	
	// Adjust viewport if cursor goes out of bounds
	if (doc_ && needs_render) {
		int cx = doc_->get_cursor_x();
		int cy = doc_->get_cursor_y();
		
		if (cy < top_line_) {
			top_line_ = cy;
		} else if (cy >= top_line_ + height_ - 2) {
			top_line_ = cy - (height_ - 2) + 1;
		}
		
		if (cx < left_column_) {
			left_column_ = cx;
		} else if (cx >= left_column_ + width_ - 2) {
			left_column_ = cx - (width_ - 2) + 1;
		}
	}
	
	return needs_render;
}

int window::get_cursor_x() const
{
	return doc_ ? doc_->get_cursor_x() : -1;
}

int window::get_cursor_y() const
{
	return doc_ ? doc_->get_cursor_y() : -1;
}

void window::draw() const
{
	draw_content();
	draw_border();
}

void window::draw_content() const
{
	attron(COLOR_PAIR(3));
	
	int sel_start_x, sel_start_y, sel_end_x, sel_end_y;
	bool has_sel = false;
	if (doc_) {
		if (doc_->has_selection()) {
			doc_->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
			has_sel = true;
		}
	}

	for (int i = 1; i < height_ - 1; ++i) {
		move(y_ + i, x_ + 1);
		
		int doc_line_idx = top_line_ + i - 1;
		std::string line_text;
		if (doc_ && doc_line_idx < static_cast<int>(doc_->get_line_count())) {
			line_text = doc_->get_line(doc_line_idx).get_text();
		}

		for (int j = 1; j < width_ - 1; ++j) {
			int text_col = left_column_ + j - 1;
			
			bool in_selection = false;
			if (has_sel) {
				if (doc_line_idx > sel_start_y && doc_line_idx < sel_end_y) {
					in_selection = true;
				} else if (doc_line_idx == sel_start_y && doc_line_idx == sel_end_y) {
					in_selection = (text_col >= sel_start_x && text_col < sel_end_x);
				} else if (doc_line_idx == sel_start_y) {
					in_selection = (text_col >= sel_start_x);
				} else if (doc_line_idx == sel_end_y) {
					in_selection = (text_col < sel_end_x);
				}
			}

			if (in_selection) attron(A_REVERSE);
			
			if (text_col < static_cast<int>(line_text.length())) {
				addch(line_text[text_col]);
			} else {
				addch(' ');
			}
			
			if (in_selection) attroff(A_REVERSE);
		}
	}
	attroff(COLOR_PAIR(3));
}

void window::draw_border() const
{
	attron(COLOR_PAIR(3));
	
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
	if (!title_.empty()) {
		int title_x = x_ + (width_ - title_.length()) / 2;
		mvprintw(y_, title_x - 1, " %s ", title_.c_str());
	}
	
	// Draw close widget
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(5));
	addstr("■");
	attroff(COLOR_PAIR(5));
	attron(COLOR_PAIR(3));
	addstr("]");
	
	// Draw window number
	mvprintw(y_, x_ + width_ - 6, "=%d=", id_);
	
	attroff(COLOR_PAIR(3));
	
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
