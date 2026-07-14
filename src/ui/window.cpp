#include "ui/window.h"
#include <algorithm>
#include <format>
#include <ncurses.h>
#include <string>
#include <filesystem>
#include "ansi.h"
#include "build_error_manager.h"
#include "codereview_manager.h"
#include "project_manager.h"
#include "event_logger.h"
#include "git_manager.h"
#include "ui/ui_element.h"
#include "utf8.h"
#include "syntax_color_manager.h"

window::window(int id, int x, int y, int width, int height, const std::string &title)
    : x_(x), y_(y), width_(width), height_(height), restore_x_(x), restore_y_(y), restore_width_(width), restore_height_(height), id_(id),
      title_(title)
{
}

void window::set_bounds(int x, int y, int width, int height)
{
	bool size_changed = (width_ != width || height_ != height);
	bool pos_changed = (x_ != x || y_ != y);
	x_ = x;
	y_ = y;
	width_ = width;
	height_ = height;

	if (!is_maximized_) {
		restore_x_ = x;
		restore_y_ = y;
		restore_width_ = width;
		restore_height_ = height;
	}

	if (pos_changed) {
		on_move(x, y);
	}
	if (size_changed) {
		on_resize(width, height);
	}
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

void window::invalidate_cursor()
{
	needs_cursor_ = true;
}

bool window::needs_cursor() const
{
	return needs_cursor_;
}

void window::clear_needs_cursor()
{
	needs_cursor_ = false;
}

bool window::process_events()
{
	needs_render_ = false;
	needs_cursor_ = false;
	while (auto ev = window_queue_.pop()) {
		event_logger::get_instance().log("Window {} processing key: {}", id_, ev->key_code);
		if (ev->type == event_type::key_press && doc_) {
			is_mouse_selecting_ = false;
			mouse_sel_start_char_ = -1;
			mouse_sel_start_line_ = -1;
			mouse_sel_end_char_ = -1;
			mouse_sel_end_line_ = -1;
			switch (ev->key_code) {
				case KEY_UP:
					doc_->move_cursor(0, -1);
					invalidate_cursor();
					break;
				case KEY_DOWN:
					doc_->move_cursor(0, 1);
					invalidate_cursor();
					break;
				case KEY_LEFT:
					doc_->move_cursor(-1, 0);
					invalidate_cursor();
					break;
				case KEY_RIGHT:
					doc_->move_cursor(1, 0);
					invalidate_cursor();
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
						doc_->move_cursor(match->second - doc_->get_cursor_x(),
								  match->first - doc_->get_cursor_y());
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
		} else if (ev->type == event_type::mouse_click && doc_) {
			// 1. Right Scrollbar (Vertical)
			if (ev->mouse_x == x_ + width_ - 1 && ev->mouse_y >= y_ + 1 && ev->mouse_y <= y_ + height_ - 2) {
				int total_lines = static_cast<int>(doc_->get_line_count());
				int content_h = std::max(1, height_ - 2);
				int max_top = std::max(0, total_lines - content_h);

				if (ev->mouse_y == y_ + 1) {
					top_line_ = std::max(0, top_line_ - 1);
				} else if (ev->mouse_y == y_ + height_ - 2) {
					top_line_ = std::min(max_top, top_line_ + 1);
				} else {
					int track_h = height_ - 4;
					if (track_h > 1) {
						double ratio = static_cast<double>(ev->mouse_y - (y_ + 2)) / (track_h - 1);
						top_line_ = static_cast<int>(ratio * max_top);
						top_line_ = std::clamp(top_line_, 0, max_top);
					}
				}

				if (doc_->get_cursor_y() < top_line_) {
					doc_->move_cursor(0, top_line_ - doc_->get_cursor_y());
				} else if (doc_->get_cursor_y() >= top_line_ + content_h) {
					doc_->move_cursor(0, (top_line_ + content_h - 1) - doc_->get_cursor_y());
				}
				invalidate();
			}
			// 2. Bottom Scrollbar (Horizontal)
			else if (ev->mouse_y == y_ + height_ - 1 && ev->mouse_x >= x_ + 1 && ev->mouse_x <= x_ + width_ - 2) {
				int max_col = 0;
				int total_lines = static_cast<int>(doc_->get_line_count());
				for (int i = 0; i < std::min(total_lines, 5000); ++i) {
					auto l = doc_->get_line(i);
					if (l) {
						max_col = std::max(max_col, l->char_to_display_col(l->length_in_chars()));
					}
				}
				int content_w = std::max(1, width_ - 2);
				int max_left = std::max(0, max_col - content_w);

				if (ev->mouse_x == x_ + 1) {
					left_column_ = std::max(0, left_column_ - 4);
				} else if (ev->mouse_x == x_ + width_ - 2) {
					left_column_ = std::min(max_left, left_column_ + 4);
				} else {
					int track_w = width_ - 4;
					if (track_w > 1) {
						double ratio = static_cast<double>(ev->mouse_x - (x_ + 2)) / (track_w - 1);
						left_column_ = static_cast<int>(ratio * max_left);
						left_column_ = std::clamp(left_column_, 0, max_left);
					}
				}

				int cur_y = doc_->get_cursor_y();
				auto l = doc_->get_line(cur_y);
				if (l) {
					int display_col = get_cursor_x();
					if (display_col < left_column_) {
						int target_char = l->display_col_to_char_pos(left_column_);
						doc_->move_cursor(target_char - doc_->get_cursor_x(), 0);
					} else if (display_col >= left_column_ + content_w) {
						int target_col = left_column_ + content_w - 1;
						int target_char = l->display_col_to_char_pos(target_col);
						doc_->move_cursor(target_char - doc_->get_cursor_x(), 0);
					}
				}
				invalidate();
			}
			// 3. Main Text Content Area
			else if (ev->mouse_x >= x_ + 1 && ev->mouse_x <= x_ + width_ - 2 &&
			         ev->mouse_y >= y_ + 1 && ev->mouse_y <= y_ + height_ - 2) {
				int click_row = top_line_ + (ev->mouse_y - y_ - 1);
				int click_col_display = left_column_ + (ev->mouse_x - x_ - 1);
				click_row = std::clamp(click_row, 0, std::max(0, static_cast<int>(doc_->get_line_count()) - 1));
				click_col_display = std::max(0, click_col_display);

				auto l = doc_->get_line(click_row);
				int click_char = 0;
				if (l) {
					click_char = l->display_col_to_char_pos(click_col_display);
				}

				doc_->move_cursor(click_char - doc_->get_cursor_x(), click_row - doc_->get_cursor_y());

				is_mouse_selecting_ = true;
				mouse_sel_start_char_ = click_char;
				mouse_sel_start_line_ = click_row;
				mouse_sel_end_char_ = click_char;
				mouse_sel_end_line_ = click_row;
				invalidate();
			}
		} else if (ev->type == event_type::mouse_drag && doc_) {
			if (is_mouse_selecting_) {
				int click_row = top_line_ + (ev->mouse_y - y_ - 1);
				int click_col_display = left_column_ + (ev->mouse_x - x_ - 1);
				click_row = std::clamp(click_row, 0, std::max(0, static_cast<int>(doc_->get_line_count()) - 1));
				click_col_display = std::max(0, click_col_display);

				auto l = doc_->get_line(click_row);
				int click_char = 0;
				if (l) {
					click_char = l->display_col_to_char_pos(click_col_display);
				}

				mouse_sel_end_char_ = click_char;
				mouse_sel_end_line_ = click_row;
				doc_->move_cursor(click_char - doc_->get_cursor_x(), click_row - doc_->get_cursor_y());
				invalidate();
			}
		} else if (ev->type == event_type::mouse_release && doc_) {
			if (is_mouse_selecting_) {
				std::string selected_text = get_mouse_selected_text();
				if (!selected_text.empty()) {
					ansi::copy_to_clipboard(selected_text);
				}
				is_mouse_selecting_ = false;
				invalidate();
			}
		} else if (ev->type == event_type::paste && doc_) {
			doc_->insert_text(ev->payload);
			invalidate();
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

bool window::draw(bool cursor_only) const
{
	bool viewport_changed = update_viewport();
	if (viewport_changed) {
		cursor_only = false;
	}
	draw_content(cursor_only);
	if (!cursor_only) {
		draw_border();
	}
	return viewport_changed;
}

bool window::update_viewport() const
{
	if (!doc_)
		return false;

	int old_top_line = top_line_;
	int old_left_column = left_column_;

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

	return (top_line_ != old_top_line || left_column_ != old_left_column);
}

void window::draw_content(bool cursor_only) const
{
	int sel_start_x, sel_start_y, sel_end_x, sel_end_y;
	std::string filename;
	bool has_sel = false;
	if (doc_ && doc_->has_selection()) {
		doc_->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
		has_sel = true;
	}

	bool has_mouse_sel = (mouse_sel_start_line_ != -1 && mouse_sel_end_line_ != -1);
	int mouse_start_l = mouse_sel_start_line_;
	int mouse_start_c = mouse_sel_start_char_;
	int mouse_end_l = mouse_sel_end_line_;
	int mouse_end_c = mouse_sel_end_char_;
	if (has_mouse_sel) {
		if (mouse_start_l > mouse_end_l || (mouse_start_l == mouse_end_l && mouse_start_c > mouse_end_c)) {
			std::swap(mouse_start_l, mouse_end_l);
			std::swap(mouse_start_c, mouse_end_c);
		}
	}

	std::optional<std::pair<int, int>> match_pos;
	if (doc_) {
		match_pos = doc_->find_matching_bracket(doc_->get_cursor_y(), doc_->get_cursor_x());
	}

	if (cursor_only) {
		bool bracket_changed = (match_pos != last_match_pos_);
		if (!has_sel && !has_mouse_sel && !bracket_changed) {
			return;
		}
	}

	last_match_pos_ = match_pos;

	if (doc_) {
		filename = doc_->get_safe_filename();
	}

	std::vector<review_item> active_reviews;
	if (doc_ && !filename.empty()) {
		std::string proj_root = project_manager::get_instance().get_project_root();
		auto file_matches = [&proj_root](const std::string &doc_filename, const std::string &item_filename) -> bool {
			if (doc_filename.empty() || item_filename.empty())
				return false;
			if (doc_filename == item_filename)
				return true;
			std::filesystem::path doc_path(doc_filename);
			std::filesystem::path item_path(item_filename);
			std::filesystem::path abs_doc = doc_path.is_absolute() ? doc_path : std::filesystem::path(proj_root) / doc_path;
			std::filesystem::path abs_item = item_path.is_absolute() ? item_path : std::filesystem::path(proj_root) / item_path;
			return abs_doc.lexically_normal() == abs_item.lexically_normal();
		};

		auto all_reviews = codereview_manager::get_instance().list_code_review_items();
		for (const auto &item : all_reviews) {
			if (item.line_number > 0 &&
			    (item.state == "new" || item.state == "confirmed" || item.state == "disputed") &&
			    file_matches(filename, item.filename)) {
				active_reviews.push_back(item);
			}
		}
	}

	for (int i = 1; i < height_ - 1; ++i) {
		int doc_line_idx = top_line_ + i - 1;
		move(y_ + i, x_ + 1);
		bool has_build_err = false;

		int line_bg_pair = background_color_pair_;
		if (doc_) {
			auto build_err = build_error_manager::get_instance().find_error_at(filename, doc_line_idx);
			if (build_err && build_err->end_column == 0) {
				has_build_err = true;
				line_bg_pair = build_err->is_warning ? 28 : 27;
			}
		}

		bool has_code_review = false;
		review_item line_review;
		if (doc_ && !has_build_err) {
			for (const auto &item : active_reviews) {
				if (item.line_number == doc_line_idx + 1) {
					if (!has_code_review) {
						has_code_review = true;
						line_review = item;
					} else {
						auto sev_val = [](const std::string &sev) {
							if (sev == "critical") return 5;
							if (sev == "high") return 4;
							if (sev == "medium") return 3;
							if (sev == "low") return 2;
							return 1;
						};
						if (sev_val(item.severity) > sev_val(line_review.severity)) {
							line_review = item;
						}
					}
				}
			}
			if (has_code_review) {
				bool is_warn = (line_review.severity == "nit" || line_review.severity == "low");
				line_bg_pair = is_warn ? 28 : 27;
			}
		}

		// Clear line background
		attrset(COLOR_PAIR(line_bg_pair));
		/* FIXME - we should do this at the end and then only for what is left on the screen, not for the whole line. The tricky
		 * part will be tabs */
		for (int k = 0; k < width_ - 2; ++k)
			addch(' ');

		if (!doc_ || doc_line_idx >= static_cast<int>(doc_->get_line_count())) {
			continue;
		}

		auto current_l = doc_->get_line(doc_line_idx);
		if (!current_l)
			continue;

		std::string line_text;
		std::vector<syntax_attribute> line_attrs;
		current_l->get_content(line_text, line_attrs);

		int current_display_col = 0;
		size_t byte_off = 0;
		int last_attr_pair = line_bg_pair;

		std::string utf8_char;
		utf8_char.reserve(4);

		for (size_t char_idx = 0;; ++char_idx) {
			int start_col = current_display_col;

			// Determine character width and content
			if (!utf8::next_character(line_text, byte_off, utf8_char))
				break;

			int char_width = 1;
			if (utf8_char == "\t") {
				char_width = line::global_tab_width - (start_col % line::global_tab_width);
			}

			int end_col = start_col + char_width;
			current_display_col = end_col;

			// Check if any part of character is within horizontal viewport
			for (int col = start_col; col < end_col; ++col) {
				if (col >= left_column_ && col < left_column_ + width_ - 2) {
					int screen_x_offset = col - left_column_;
					move(y_ + i, x_ + 1 + screen_x_offset);

					bool in_block_selection = false;
					if (has_sel) {
						if (doc_line_idx > sel_start_y && doc_line_idx < sel_end_y) {
							in_block_selection = true;
						} else if (doc_line_idx == sel_start_y && doc_line_idx == sel_end_y) {
							in_block_selection = (static_cast<int>(char_idx) >= sel_start_x &&
									      static_cast<int>(char_idx) < sel_end_x);
						} else if (doc_line_idx == sel_start_y) {
							in_block_selection = (static_cast<int>(char_idx) >= sel_start_x);
						} else if (doc_line_idx == sel_end_y) {
							in_block_selection = (static_cast<int>(char_idx) < sel_end_x);
						}
					}

					bool in_mouse_selection = false;
					if (has_mouse_sel) {
						if (doc_line_idx > mouse_start_l && doc_line_idx < mouse_end_l) {
							in_mouse_selection = true;
						} else if (doc_line_idx == mouse_start_l && doc_line_idx == mouse_end_l) {
							in_mouse_selection = (static_cast<int>(char_idx) >= mouse_start_c &&
									      static_cast<int>(char_idx) < mouse_end_c);
						} else if (doc_line_idx == mouse_start_l) {
							in_mouse_selection = (static_cast<int>(char_idx) >= mouse_start_c);
						} else if (doc_line_idx == mouse_end_l) {
							in_mouse_selection = (static_cast<int>(char_idx) < mouse_end_c);
						}
					}

					bool in_selection = (in_block_selection != in_mouse_selection);

					bool is_match = false;
					if (match_pos) {
						if ((doc_line_idx == match_pos->first && static_cast<int>(char_idx) == match_pos->second) ||
						    (doc_line_idx == doc_->get_cursor_y() &&
						     static_cast<int>(char_idx) == doc_->get_cursor_x())) {
							is_match = true;
						}
					}

					bool is_lsp_highlight = false;
					if (doc_) {
						for (const auto &hl : doc_->get_lsp_highlights()) {
							if (doc_line_idx > hl.start_y && doc_line_idx < hl.end_y) {
								is_lsp_highlight = true;
								break;
							} else if (doc_line_idx == hl.start_y && doc_line_idx == hl.end_y) {
								if (static_cast<int>(char_idx) >= hl.start_x &&
								    static_cast<int>(char_idx) < hl.end_x) {
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
						for (const auto &diag : doc_->get_lsp_diagnostics()) {
							if (doc_line_idx > diag.range.start_y && doc_line_idx < diag.range.end_y) {
								diagnostic_severity = diag.severity;
								break;
							} else if (doc_line_idx == diag.range.start_y && doc_line_idx == diag.range.end_y) {
								if (static_cast<int>(char_idx) >= diag.range.start_x &&
								    static_cast<int>(char_idx) < diag.range.end_x) {
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

					syntax_attribute attr = syntax_attribute::normal;
					if (char_idx < line_attrs.size()) {
						attr = line_attrs[char_idx];
					}
					int pair = line_bg_pair;
					if (is_match) {
						pair = 13; // Bright Yellow on Cyan
					} else if (in_selection) {
						pair = 8;
						if (attr == syntax_attribute::keyword)
							pair = 13;
					} else {
						// Build error/warning might be active for this line as a sub-line highlight.
						if (doc_ && has_build_err) {
							auto build_err =
							    build_error_manager::get_instance().find_error_at(filename, doc_line_idx);
							if (build_err) {
								if (build_err->end_column == 0 ||
								    (static_cast<int>(char_idx) >= build_err->column &&
								     static_cast<int>(char_idx) < build_err->end_column)) {
									pair = build_err->is_warning ? 28 : 27;
								}
							}
						} else if (doc_ && has_code_review) {
							bool is_warn = (line_review.severity == "nit" || line_review.severity == "low");
							pair = is_warn ? 28 : 27;
						}
					}

					// If no build error override happened, fallback to standard diagnostics/highlights
					if (pair == line_bg_pair && line_bg_pair == background_color_pair_) {
						if (diagnostic_severity == 1) {	       // Error
							pair = 27;		       // White on Red
						} else if (diagnostic_severity == 2) { // Warning
							pair = 28;		       // Black on Yellow
						} else if (is_lsp_highlight) {
							pair = 25; // Normal on Magenta
							if (attr == syntax_attribute::keyword)
								pair = 26; // Keyword on Magenta
						} else {
							pair = syntax_color_manager::get_instance().get_color_pair(attr);
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
	int border_pair = is_active() ? 5 : 38;
	int widget_pair = is_active() ? 3 : 39;

	std::string current_title = get_displayed_title();

	ui_utils::draw_border(x_, y_, width_, height_, ui_utils::border_style::double_line, border_pair);
	attrset(COLOR_PAIR(border_pair));

	// Title

	if (!current_title.empty()) {
		int title_x = x_ + (width_ - current_title.length()) / 2;
		attron(COLOR_PAIR(5));
		mvprintw(y_, title_x - 1, " %s ", current_title.c_str());
		attroff(COLOR_PAIR(5));
		attron(COLOR_PAIR(border_pair));
	}

	// Draw close widget
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(widget_pair));
	addstr("■");
	attroff(COLOR_PAIR(widget_pair));
	attron(COLOR_PAIR(border_pair));
	addstr("]");

	// Draw Git Status and Branch
	if (doc_ && !doc_->get_filename().empty() && doc_->get_filename() != "unknown.txt") {
		git_info info = git_manager::get_instance().get_cached_info(doc_->get_filename());
		std::string branch = doc_->get_git_branch();
		if (branch.empty())
			branch = "unknown";

		std::string indicator = "?";
		int pair = border_pair;
		if (info.status == git_status::clean) {
			indicator = "✔";
			pair = 20;
		} else if (info.status == git_status::dirty) {
			indicator = "✎";
			pair = 21;
		}

		mvaddstr(y_, x_ + 6, "[");
		attron(COLOR_PAIR(border_pair));
		addstr(branch.c_str());
		addch(' ');
		attron(COLOR_PAIR(pair));
		addstr(indicator.c_str());
		attroff(COLOR_PAIR(pair));
		attron(COLOR_PAIR(border_pair));
		addstr("]");
	}

	// Draw popup menu widget
	mvaddstr(y_, x_ + width_ - 10, "[");
	attron(COLOR_PAIR(widget_pair));
	addstr("≡");
	attroff(COLOR_PAIR(widget_pair));
	attron(COLOR_PAIR(border_pair));
	addstr("]");

	// Draw window number
	mvprintw(y_, x_ + width_ - 6, "=%d=", id_);

	attroff(COLOR_PAIR(border_pair));
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

	// Draw vertical scrollbar thumb
	int total_lines = get_scroll_total_lines();
	int top_line = get_scroll_top_line();
	int content_h = get_scroll_content_height();
	int track_h = height_ - 4;
	if (total_lines > content_h && track_h > 0) {
		int max_top = total_lines - content_h;
		double ratio = static_cast<double>(top_line) / max_top;
		int thumb_pos = static_cast<int>(ratio * (track_h - 1) + 0.5);
		thumb_pos = std::clamp(thumb_pos, 0, track_h - 1);
		mvaddstr(y_ + 2 + thumb_pos, x_ + width_ - 1, "█");
	}
	attroff(COLOR_PAIR(4));
}

int window::get_git_button_width() const
{
	if (doc_ && !doc_->get_filename().empty() && doc_->get_filename() != "unknown.txt") {
		std::string branch = doc_->get_git_branch();
		if (branch.empty())
			branch = "unknown";
		return branch.length() + 4;
	}
	return 0;
}

window::~window()
{
	for (auto *other : linked_windows_) {
		if (other) {
			other->unlink_window(this);
		}
	}
}

int window::get_display_priority() const
{
	for (const auto *w : linked_windows_) {
		if (w && w->is_active()) {
			return 9998;
		}
	}
	return display_priority_;
}

void window::link_window(window *other)
{
	if (other && std::find(linked_windows_.begin(), linked_windows_.end(), other) == linked_windows_.end()) {
		linked_windows_.push_back(other);
		other->link_window(this);
	}
}

void window::unlink_window(window *other)
{
	auto it = std::find(linked_windows_.begin(), linked_windows_.end(), other);
	if (it != linked_windows_.end()) {
		linked_windows_.erase(it);
	}
}

std::string window::get_mouse_selected_text() const
{
	if (!doc_ || mouse_sel_start_line_ == -1 || mouse_sel_end_line_ == -1) {
		return "";
	}

	int start_l = mouse_sel_start_line_;
	int start_c = mouse_sel_start_char_;
	int end_l = mouse_sel_end_line_;
	int end_c = mouse_sel_end_char_;

	// Swap if start is after end
	if (start_l > end_l || (start_l == end_l && start_c > end_c)) {
		std::swap(start_l, end_l);
		std::swap(start_c, end_c);
	}

	std::string result;
	for (int l = start_l; l <= end_l; ++l) {
		auto line_obj = doc_->get_line(l);
		if (!line_obj)
			continue;
		std::string line_text = line_obj->get_text();
		int len = line_obj->length_in_chars();

		int from_c = (l == start_l) ? start_c : 0;
		int to_c = (l == end_l) ? end_c : len;

		// Ensure from_c and to_c are within bounds
		from_c = std::clamp(from_c, 0, len);
		to_c = std::clamp(to_c, 0, len);

		if (from_c < to_c) {
			size_t from_byte = line_obj->char_to_byte_offset(from_c);
			size_t to_byte = line_obj->char_to_byte_offset(to_c);
			result += line_text.substr(from_byte, to_byte - from_byte);
		}

		if (l < end_l) {
			result += "\n";
		}
	}
	return result;
}

int window::get_scroll_total_lines() const
{
	return doc_ ? static_cast<int>(doc_->get_line_count()) : 0;
}

int window::get_scroll_top_line() const
{
	return doc_ ? top_line_ : 0;
}

int window::get_scroll_content_height() const
{
	return get_content_height();
}