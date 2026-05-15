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
	for (int i = 1; i < height_ - 1; ++i) {
		move(y_ + i, x_ + 1);
		
		int doc_line_idx = top_line_ + i - 1;
		std::string line_text;
		if (doc_ && doc_line_idx < static_cast<int>(doc_->get_line_count())) {
			line_text = doc_->get_line(doc_line_idx).get_text();
		}

		for (int j = 1; j < width_ - 1; ++j) {
			int text_col = left_column_ + j - 1;
			if (text_col < static_cast<int>(line_text.length())) {
				addch(line_text[text_col]);
			} else {
				addch(' ');
			}
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
