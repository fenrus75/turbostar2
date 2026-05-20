#include "window.h"
#include <ncurses.h>
#include "event_logger.h"
#include "build_error_manager.h"
#include "git_manager.h"

window::window(int id, int x, int y, int width, int height, const std::string &title)
    : x_(x), y_(y), width_(width), height_(height), id_(id), title_(title)
{
}

void window::update_last_active_timestamp()
{
	auto now = std::chrono::steady_clock::now().time_since_epoch();
	last_active_timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

void window::set_active(bool active)
{
	is_active_ = active;
	if (is_active_) {
		update_last_active_timestamp();
	}
}

bool window::is_active() const
{
	return is_active_;
}

event_queue &window::get_queue()
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
		int display_col;
		if (l) {
			display_col = l->char_to_display_col(doc_->get_cursor_x());
		} else {
			display_col = 0;
		}
		int screen_y = y_ + 1 + doc_->get_cursor_y() - top_line_;
		int screen_x = x_ + 1 + display_col - left_column_;
		move(screen_y, screen_x);
	}
}

void window::invalidate()
{
	needs_render_ = true;
}

bool window::process_events()
{
	needs_render_ = false;
	while (auto ev = window_queue_.pop()) {
		event_logger::get_instance().log("Window " + std::to_string(id_) + " processing key: " + std::to_string(ev->key_code));
		if (ev->type == event_type::key_press && doc_) {
			switch (ev->key_code) {
				case KEY_UP:
					doc_->move_cursor(0, -1);
					invalidate();
					break;
				case KEY_DOWN:
					doc_->move_cursor(0, 1);
					invalidate();
					break;
				case KEY_LEFT:
					doc_->move_cursor(-1, 0);
					invalidate();
					break;
				case KEY_RIGHT:
					doc_->move_cursor(1, 0);
					invalidate();
					break;
				case KEY_HOME:
					doc_->move_to_bol();
					invalidate();
					break;
				case KEY_END:
					doc_->move_to_eol();
					invalidate();
					break;
				case KEY_PPAGE:
					doc_->move_page_up(height_ - 2);
					invalidate();
					break;
				case KEY_NPAGE:
					doc_->move_page_down(height_ - 2);
					invalidate();
					break;
				case KEY_DC:
					doc_->delete_char();
					invalidate();
					break;
				case KEY_BACKSPACE:
				case 127:
				case 8:
					doc_->backspace();
					invalidate();
					break;
				case 13:
				case KEY_ENTER:
					doc_->split_line();
					invalidate();
					break;
				case 10: // Ctrl-J
					doc_->delete_to_eol();
					invalidate();
					break;
				case -111:
				case -79: // Alt-O
					doc_->delete_to_bol();
					invalidate();
					break;
				case 25: // Ctrl-Y
					doc_->delete_line();
					invalidate();
					break;
				case 1: // Ctrl-A
					doc_->move_to_bol();
					invalidate();
					break;
				case 5: // Ctrl-E
					doc_->move_to_eol();
					invalidate();
					break;
				case 4: // Ctrl-D
					doc_->delete_char();
					invalidate();
					break;
				case 21: // Ctrl-U
					doc_->move_page_up(get_content_height());
					invalidate();
					break;
				case 22: // Ctrl-V
					doc_->move_page_down(get_content_height());
					invalidate();
					break;
				case 24: // Ctrl-X
					doc_->move_next_word();
					invalidate();
					break;
				case 26: // Ctrl-Z
					doc_->move_prev_word();
					invalidate();
					break;
				case 23: // Ctrl-W
					doc_->delete_word_forward();
					invalidate();
					break;
				case 15: // Ctrl-O
					doc_->delete_word_backward();
					invalidate();
					break;
				case 7: { // Ctrl-G (Matching bracket)
					auto match = doc_->find_matching_bracket(doc_->get_cursor_y(), doc_->get_cursor_x());
					if (match) {
						doc_->move_cursor(match->second - doc_->get_cursor_x(), match->first - doc_->get_cursor_y());
						invalidate();
					}
					break;
				}
				default:
					if (!ev->utf8_char.empty() && (ev->key_code >= 32 || ev->key_code == 9) && ev->key_code != 127) {
						doc_->insert_char(ev->utf8_char);
						invalidate();
					}
					break;
			}
		}
	}

	return needs_render_;
}

int window::get_cursor_x() const
{
	if (!doc_)
		return -1;
	auto l = doc_->get_line(doc_->get_cursor_y());
	return l ? l->char_to_display_col(doc_->get_cursor_x()) : 0;
}

int window::get_cursor_y() const
{
	int result;
	if (doc_) {
		result = doc_->get_cursor_y();
	} else {
		result = -1;
	}
	return result;
}

void window::draw() const
{
	update_viewport();
	draw_content();
	draw_border();
}

void window::update_viewport() const
{
	if (!doc_)
		return;

	auto l = doc_->get_line(doc_->get_cursor_y());
	int display_col;
	if (l) {
		display_col = l->char_to_display_col(doc_->get_cursor_x());
	} else {
		display_col = 0;
	}
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

	std::optional<std::pair<int, int>> match_pos;
	if (doc_) {
		match_pos = doc_->find_matching_bracket(doc_->get_cursor_y(), doc_->get_cursor_x());
	}

	for (int i = 1; i < height_ - 1; ++i) {
		int doc_line_idx = top_line_ + i - 1;
		move(y_ + i, x_ + 1);

		int line_bg_pair = background_color_pair_;
		if (doc_) {
			auto build_err = build_error_manager::get_instance().find_error_at(doc_->get_safe_filename(), doc_line_idx);
			if (build_err && build_err->end_column == 0) {
				line_bg_pair = build_err->is_warning ? 28 : 27;
			}
		}

		// Clear line background
		attrset(COLOR_PAIR(line_bg_pair));
		for (int k = 0; k < width_ - 2; ++k)
			addch(' ');

		if (!doc_ || doc_line_idx >= static_cast<int>(doc_->get_line_count())) {
			continue;
		}

		auto current_l = doc_->get_line(doc_line_idx);
		if (!current_l)
			continue;

		int current_display_col = 0;
		size_t byte_off = 0;
		int last_attr_pair = line_bg_pair;

		std::string utf8_char;
		utf8_char.reserve(4);

		for (size_t char_idx = 0; ; ++char_idx) {
			int start_col = current_display_col;

			// Determine character width and content
			if (!current_l->next_utf8_character(byte_off, utf8_char))
				break;

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
							in_selection = (static_cast<int>(char_idx) >= sel_start_x &&
									static_cast<int>(char_idx) < sel_end_x);
						} else if (doc_line_idx == sel_start_y) {
							in_selection = (static_cast<int>(char_idx) >= sel_start_x);
						} else if (doc_line_idx == sel_end_y) {
							in_selection = (static_cast<int>(char_idx) < sel_end_x);
						}
					}

					bool is_match = false;
					if (match_pos) {
						if ((doc_line_idx == match_pos->first && static_cast<int>(char_idx) == match_pos->second) ||
						    (doc_line_idx == doc_->get_cursor_y() && static_cast<int>(char_idx) == doc_->get_cursor_x())) {
							is_match = true;
						}
					}

					bool is_lsp_highlight = false;
					if (doc_) {
						for (const auto& hl : doc_->get_lsp_highlights()) {
							if (doc_line_idx > hl.start_y && doc_line_idx < hl.end_y) {
								is_lsp_highlight = true;
								break;
							} else if (doc_line_idx == hl.start_y && doc_line_idx == hl.end_y) {
								if (static_cast<int>(char_idx) >= hl.start_x && static_cast<int>(char_idx) < hl.end_x) {
									is_lsp_highlight = true;
									break;
								}
							} else if (doc_line_idx == hl.start_y) {
								if (static_cast<int>(char_idx) >= hl.start_x) {
									is_lsp_highlight = true;
									break;
								}
							} else if (doc_line_idx == hl.end_y) {
								if (static_cast<int>(char_idx) < hl.end_x) {
									is_lsp_highlight = true;
									break;
								}
							}
						}
					}

					int diagnostic_severity = 0;
					if (doc_) {
						for (const auto& diag : doc_->get_lsp_diagnostics()) {
							if (doc_line_idx > diag.range.start_y && doc_line_idx < diag.range.end_y) {
								diagnostic_severity = diag.severity;
								break;
							} else if (doc_line_idx == diag.range.start_y && doc_line_idx == diag.range.end_y) {
								if (static_cast<int>(char_idx) >= diag.range.start_x && static_cast<int>(char_idx) < diag.range.end_x) {
									diagnostic_severity = diag.severity;
									break;
								}
							} else if (doc_line_idx == diag.range.start_y) {
								if (static_cast<int>(char_idx) >= diag.range.start_x) {
									diagnostic_severity = diag.severity;
									break;
								}
							} else if (doc_line_idx == diag.range.end_y) {
								if (static_cast<int>(char_idx) < diag.range.end_x) {
									diagnostic_severity = diag.severity;
									break;
								}
							}
						}
					}

					syntax_attribute attr = current_l->get_attribute(char_idx);
					int pair = line_bg_pair;
					if (is_match) {
						pair = 13; // Bright Yellow on Cyan
					} else if (in_selection) {
						pair = 8;
						if (attr == syntax_attribute::keyword)
							pair = 13;
					} else {
						// Build error/warning might be active for this line as a sub-line highlight.
						if (doc_) {
							auto build_err = build_error_manager::get_instance().find_error_at(doc_->get_safe_filename(), doc_line_idx);
							if (build_err) {
								if (build_err->end_column == 0 || (static_cast<int>(char_idx) >= build_err->column && static_cast<int>(char_idx) < build_err->end_column)) {
									pair = build_err->is_warning ? 28 : 27;
								}
							}
						}
					}
					
					// If no build error override happened, fallback to standard diagnostics/highlights
					if (pair == line_bg_pair && line_bg_pair == background_color_pair_) {
						if (diagnostic_severity == 1) { // Error
							pair = 27; // White on Red
						} else if (diagnostic_severity == 2) { // Warning
							pair = 28; // Black on Yellow
						} else if (is_lsp_highlight) {
							pair = 25; // Normal on Magenta
							if (attr == syntax_attribute::keyword)
								pair = 26; // Keyword on Magenta
						} else {
							if (attr == syntax_attribute::keyword)
								pair = 12;
							else if (attr == syntax_attribute::heading)
								pair = 22;
							else if (attr == syntax_attribute::bold)
								pair = 23;
							else if (attr == syntax_attribute::list_item)
								pair = 24;
							else if (attr == syntax_attribute::trailing_space)
								pair = 27; // White on Red
						}
					}

					if (pair != last_attr_pair) {
						attrset(COLOR_PAIR(pair));
						last_attr_pair = pair;
					}
					
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

std::string window::get_displayed_title() const
{
	std::string current_title = title_;
	if (doc_) {
		current_title = doc_->get_filename();
		if (current_title.empty())
			current_title = "untitled";
		size_t last_slash = current_title.find_last_of("/\\");
		if (last_slash != std::string::npos) {
			current_title = current_title.substr(last_slash + 1);
		}

		if (doc_->is_modified()) {
			current_title += "*";
		}
	}
	return current_title;
}

void window::draw_border() const
{
	attrset(COLOR_PAIR(5));

	std::string current_title = get_displayed_title();

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

	// Title

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

	// Draw Git Status and Branch
	if (doc_ && !doc_->get_filename().empty() && doc_->get_filename() != "unknown.txt") {
		git_info info = git_manager::get_instance().get_cached_info(doc_->get_filename());
		std::string branch = doc_->get_git_branch();
		if (branch.empty()) branch = "unknown";
		
		std::string indicator = "?";
		int pair = 5;
		if (info.status == git_status::clean) {
			indicator = "✔";
			pair = 20;
		} else if (info.status == git_status::dirty) {
			indicator = "✎";
			pair = 21;
		}
		
		mvaddstr(y_, x_ + 6, "[");
		attron(COLOR_PAIR(5));
		addstr(branch.c_str());
		addch(' ');
		attron(COLOR_PAIR(pair));
		addstr(indicator.c_str());
		attroff(COLOR_PAIR(pair));
		attron(COLOR_PAIR(5));
		addstr("]");
	}

	// Draw popup menu widget
	mvaddstr(y_, x_ + width_ - 10, "[");
	attron(COLOR_PAIR(3)); // Bright Yellow
	addstr("≡");
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

int window::get_git_button_width() const {
	if (doc_ && !doc_->get_filename().empty() && doc_->get_filename() != "unknown.txt") {
		std::string branch = doc_->get_git_branch();
		if (branch.empty()) branch = "unknown";
		return branch.length() + 4;
	}
	return 0;
}
