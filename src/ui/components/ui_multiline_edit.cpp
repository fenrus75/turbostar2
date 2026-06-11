#include "ui_multiline_edit.h"
#include <cctype>
#include <ncurses.h>
#include "ansi.h"

ui_multiline_edit::ui_multiline_edit(std::string name, int x, int y, int width, int height,
				     std::function<void(const std::string &)> on_submit)
    : ui_element(std::move(name), x, y, width, height), on_submit_(std::move(on_submit))
{
}

void ui_multiline_edit::set_buffer(const std::string &text)
{
	buffer_ = text;
	cursor_pos_ = buffer_.length();
	selection_start_ = -1;
	selection_end_ = -1;
	selection_is_persistent_ = false;
	update_scroll();
}

void ui_multiline_edit::pos_to_coord(size_t pos, size_t &line_idx, size_t &col) const
{
	if (visual_lines_.empty()) {
		line_idx = 0;
		col = 0;
		return;
	}

	for (size_t i = 0; i < visual_lines_.size(); ++i) {
		const auto &vl = visual_lines_[i];
		if (i == visual_lines_.size() - 1) {
			line_idx = i;
			if (pos >= vl.start_idx) {
				col = pos - vl.start_idx;
			} else {
				col = 0;
			}
			return;
		}

		if (pos >= vl.start_idx && pos < vl.start_idx + vl.length) {
			line_idx = i;
			col = pos - vl.start_idx;
			return;
		}
	}

	line_idx = visual_lines_.size() - 1;
	if (pos >= visual_lines_.back().start_idx) {
		col = pos - visual_lines_.back().start_idx;
	} else {
		col = 0;
	}
}

size_t ui_multiline_edit::coord_to_pos(size_t line_idx, size_t col) const
{
	if (visual_lines_.empty())
		return 0;
	if (line_idx >= visual_lines_.size())
		line_idx = visual_lines_.size() - 1;

	const auto &vl = visual_lines_[line_idx];
	size_t max_col = vl.ends_with_newline ? (vl.length > 0 ? vl.length - 1 : 0) : vl.length;
	if (col > max_col)
		col = max_col;

	return vl.start_idx + col;
}

void ui_multiline_edit::delete_selection()
{
	if (selection_start_ == -1 || selection_end_ == -1)
		return;
	size_t sel_min = std::min(selection_start_, selection_end_);
	size_t sel_max = std::max(selection_start_, selection_end_);
	if (sel_max > sel_min) {
		buffer_.erase(sel_min, sel_max - sel_min);
		cursor_pos_ = sel_min;
	}
	selection_start_ = -1;
	selection_end_ = -1;
	selection_is_persistent_ = false;
	update_scroll();
}

void ui_multiline_edit::copy_selection()
{
	if (selection_start_ == -1 || selection_end_ == -1)
		return;
	size_t sel_min = std::min(selection_start_, selection_end_);
	size_t sel_max = std::max(selection_start_, selection_end_);
	if (sel_max > sel_min) {
		std::string selected_text = buffer_.substr(sel_min, sel_max - sel_min);
		ansi::copy_to_clipboard(selected_text);
	}
}

void ui_multiline_edit::cut_selection()
{
	if (selection_start_ == -1 || selection_end_ == -1)
		return;
	size_t sel_min = std::min(selection_start_, selection_end_);
	size_t sel_max = std::max(selection_start_, selection_end_);
	if (sel_max > sel_min) {
		std::string selected_text = buffer_.substr(sel_min, sel_max - sel_min);
		ansi::copy_to_clipboard(selected_text);
		buffer_.erase(sel_min, sel_max - sel_min);
		cursor_pos_ = sel_min;
	}
	selection_start_ = -1;
	selection_end_ = -1;
	selection_is_persistent_ = false;
	update_scroll();
}

void ui_multiline_edit::update_scroll()
{
	// 1. Recompute visual wrapping
	visual_lines_.clear();
	size_t N = buffer_.length();
	size_t W = width_ - 2;

	if (N == 0) {
		visual_lines_.push_back({0, 0, false, false});
	} else {
		bool is_para_start = true;
		size_t curr = 0;
		while (curr < N) {
			size_t next_nl = buffer_.find('\n', curr);
			size_t para_end = (next_nl == std::string::npos) ? N : next_nl;

			size_t para_curr = curr;
			bool is_continuation = !is_para_start;
			while (para_curr < para_end) {
				size_t rem = para_end - para_curr;
				if (rem <= W) {
					bool ends_with_nl = (para_end == next_nl);
					size_t len = rem + (ends_with_nl ? 1 : 0);
					visual_lines_.push_back({para_curr, len, is_continuation, ends_with_nl});
					para_curr = para_end + (ends_with_nl ? 1 : 0);
				} else {
					size_t space_pos = std::string::npos;
					for (size_t i = para_curr + W; i > para_curr; --i) {
						if (buffer_[i] == ' ') {
							space_pos = i;
							break;
						}
					}

					if (space_pos != std::string::npos) {
						size_t len = space_pos - para_curr + 1;
						visual_lines_.push_back({para_curr, len, is_continuation, false});
						para_curr = space_pos + 1;
					} else {
						visual_lines_.push_back({para_curr, W, is_continuation, false});
						para_curr = para_curr + W;
					}
				}
				is_continuation = true;
			}

			if (para_curr == next_nl && next_nl != std::string::npos) {
				visual_lines_.push_back({para_curr, 1, !is_para_start, true});
				para_curr++;
			}

			curr = para_curr;
			is_para_start = false;
		}

		if (buffer_.back() == '\n') {
			visual_lines_.push_back({N, 0, true, false});
		}
	}

	// 2. Map cursor position to visual coordinate
	size_t cursor_vl_idx = 0;
	size_t cursor_col = 0;
	pos_to_coord(cursor_pos_, cursor_vl_idx, cursor_col);

	// 3. Adjust scroll offset to keep cursor in view
	if ((int)cursor_vl_idx < scroll_offset_) {
		scroll_offset_ = (int)cursor_vl_idx;
	} else if ((int)cursor_vl_idx >= scroll_offset_ + height_) {
		scroll_offset_ = (int)cursor_vl_idx - height_ + 1;
	}

	// Clamp scroll offset to valid range
	int max_scroll = std::max(0, (int)visual_lines_.size() - height_);
	if (scroll_offset_ > max_scroll) {
		scroll_offset_ = max_scroll;
	}
	if (scroll_offset_ < 0) {
		scroll_offset_ = 0;
	}
}

void ui_multiline_edit::draw(int abs_x, int abs_y) const
{
	int start_y = abs_y;
	int start_x = abs_x;

	// Clear area
	attrset(COLOR_PAIR(1));
	for (int i = 0; i < height_; ++i) {
		move(start_y + i, start_x);
		for (int j = 0; j < width_; ++j) {
			addch(' ');
		}
	}

	size_t cursor_vl_idx = 0;
	size_t cursor_col = 0;
	pos_to_coord(cursor_pos_, cursor_vl_idx, cursor_col);

	for (int row = 0; row < height_; ++row) {
		int vl_idx = scroll_offset_ + row;
		if (vl_idx >= (int)visual_lines_.size()) {
			break;
		}

		const auto &vl = visual_lines_[vl_idx];
		move(start_y + row, start_x);

		attrset(COLOR_PAIR(1));
		if (!vl.is_continuation) {
			addstr("> ");
		} else {
			addstr("  ");
		}

		size_t W = width_ - 2;
		for (size_t col = 0; col < W; ++col) {
			size_t char_offset = vl.start_idx + col;
			size_t max_content_len = vl.ends_with_newline ? (vl.length > 0 ? vl.length - 1 : 0) : vl.length;
			if (col >= max_content_len) {
				break;
			}

			char c = buffer_[char_offset];

			bool is_selected = false;
			if (selection_start_ != -1 && selection_end_ != -1) {
				size_t sel_min = std::min(selection_start_, selection_end_);
				size_t sel_max = std::max(selection_start_, selection_end_);
				if (char_offset >= sel_min && char_offset < sel_max) {
					is_selected = true;
				}
			}

			if (is_selected) {
				attrset(COLOR_PAIR(8)); // White on Cyan
			} else {
				attrset(COLOR_PAIR(1));
			}

			addch(c);
		}
	}

	if (has_focus_) {
		int cursor_row = (int)cursor_vl_idx - scroll_offset_;
		if (cursor_row >= 0 && cursor_row < height_) {
			int cursor_col_screen = 2 + (int)cursor_col;
			move(start_y + cursor_row, start_x + cursor_col_screen);
		}
	}
}

bool ui_multiline_edit::handle_event(const editor_event &ev, int /*abs_x*/, int /*abs_y*/)
{
	std::string orig_buffer = buffer_;
	bool handled = false;

	if (ev.type == event_type::key_press) {
		int key = ev.key_code;

		if (key == 11) { // Ctrl-K
			k_block_mode_ = true;
			handled = true;
		} else if (k_block_mode_) {
			k_block_mode_ = false;
			handled = true;
			if (key == 'b' || key == 'B') {
				selection_start_ = cursor_pos_;
				selection_is_persistent_ = true;
			} else if (key == 'k' || key == 'K') {
				selection_end_ = cursor_pos_;
				selection_is_persistent_ = true;
			} else if (key == 'y' || key == 'Y') {
				delete_selection();
			} else if (key == 'c' || key == 'C') {
				copy_selection();
			} else if (key == 'h' || key == 'H') {
				selection_start_ = -1;
				selection_end_ = -1;
				selection_is_persistent_ = false;
			} else {
				handled = false;
			}
		} else {
			bool is_nav = false;
			bool shift = false;
			enum class nav_action { none, left, right, up, down, home, end, next_word, prev_word };
			nav_action action = nav_action::none;

			if (key == KEY_LEFT) {
				is_nav = true; shift = false; action = nav_action::left;
			} else if (key == KEY_SLEFT) {
				is_nav = true; shift = true; action = nav_action::left;
			} else if (key == KEY_RIGHT) {
				is_nav = true; shift = false; action = nav_action::right;
			} else if (key == KEY_SRIGHT) {
				is_nav = true; shift = true; action = nav_action::right;
			} else if (key == KEY_UP) {
				is_nav = true; shift = false; action = nav_action::up;
			} else if (key == KEY_SR) {
				is_nav = true; shift = true; action = nav_action::up;
			} else if (key == KEY_DOWN) {
				is_nav = true; shift = false; action = nav_action::down;
			} else if (key == KEY_SF) {
				is_nav = true; shift = true; action = nav_action::down;
			} else if (key == KEY_HOME || key == 1) { // Home or Ctrl-A
				is_nav = true; shift = false; action = nav_action::home;
			} else if (key == KEY_SHOME) {
				is_nav = true; shift = true; action = nav_action::home;
			} else if (key == KEY_END || key == 5) { // End or Ctrl-E
				is_nav = true; shift = false; action = nav_action::end;
			} else if (key == KEY_SEND) {
				is_nav = true; shift = true; action = nav_action::end;
			} else if (key == 24) { // Ctrl-X
				if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
					cut_selection();
					handled = true;
				} else {
					is_nav = true; shift = false; action = nav_action::next_word;
				}
			} else if (key == 26) { // Ctrl-Z
				is_nav = true; shift = false; action = nav_action::prev_word;
			} else if (key == 3) { // Ctrl-C
				if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
					copy_selection();
					handled = true;
				}
			}

			if (is_nav) {
				if (shift) {
					if (selection_start_ == -1) {
						selection_start_ = (int)cursor_pos_;
					}
				} else {
					if (!selection_is_persistent_) {
						selection_start_ = -1;
						selection_end_ = -1;
					}
				}

				size_t cursor_vl_idx = 0;
				size_t cursor_col = 0;
				pos_to_coord(cursor_pos_, cursor_vl_idx, cursor_col);

				bool moved = false;

				switch (action) {
					case nav_action::left:
						if (cursor_pos_ > 0) {
							cursor_pos_--;
							moved = true;
						}
						break;
					case nav_action::right:
						if (cursor_pos_ < static_cast<int>(buffer_.length())) {
							cursor_pos_++;
							moved = true;
						}
						break;
					case nav_action::up:
						if (cursor_vl_idx > 0) {
							cursor_pos_ = coord_to_pos(cursor_vl_idx - 1, cursor_col);
							moved = true;
						}
						break;
					case nav_action::down:
						if (cursor_vl_idx < visual_lines_.size() - 1) {
							cursor_pos_ = coord_to_pos(cursor_vl_idx + 1, cursor_col);
							moved = true;
						}
						break;
					case nav_action::home:
						if (!visual_lines_.empty()) {
							const auto &vl = visual_lines_[cursor_vl_idx];
							cursor_pos_ = vl.start_idx;
							moved = true;
						}
						break;
					case nav_action::end:
						if (!visual_lines_.empty()) {
							const auto &vl = visual_lines_[cursor_vl_idx];
							cursor_pos_ = vl.start_idx + (vl.ends_with_newline && vl.length > 0 ? vl.length - 1 : vl.length);
							moved = true;
						}
						break;
					case nav_action::next_word:
						{
							size_t len = buffer_.length();
							size_t i = cursor_pos_;
							while (i < len && !isspace(static_cast<unsigned char>(buffer_[i]))) {
								i++;
							}
							while (i < len && isspace(static_cast<unsigned char>(buffer_[i]))) {
								i++;
							}
							cursor_pos_ = i;
							moved = true;
						}
						break;
					case nav_action::prev_word:
						if (cursor_pos_ > 0) {
							size_t i = cursor_pos_ - 1;
							while (i > 0 && isspace(static_cast<unsigned char>(buffer_[i]))) {
								i--;
							}
							while (i > 0 && !isspace(static_cast<unsigned char>(buffer_[i - 1]))) {
								i--;
							}
							cursor_pos_ = i;
							moved = true;
						}
						break;
					default:
						break;
				}

				if (moved) {
					update_scroll();
					if (shift) {
						selection_end_ = (int)cursor_pos_;
					}
					handled = true;
				} else {
					if (action == nav_action::up || action == nav_action::down) {
						handled = false;
					} else {
						handled = true;
					}
				}
			} else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
				if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
					delete_selection();
					handled = true;
				} else if (cursor_pos_ > 0) {
					buffer_.erase(cursor_pos_ - 1, 1);
					cursor_pos_--;
					update_scroll();
					handled = true;
				}
			} else if (key == KEY_DC) {
				if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
					delete_selection();
					handled = true;
				} else if (cursor_pos_ < (int)buffer_.length()) {
					buffer_.erase(cursor_pos_, 1);
					update_scroll();
					handled = true;
				}
			} else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
				if (on_submit_ && !buffer_.empty()) {
					selection_start_ = -1;
					selection_end_ = -1;
					selection_is_persistent_ = false;
					on_submit_(buffer_);
					buffer_.clear();
					cursor_pos_ = 0;
					update_scroll();
				}
				handled = true;
			} else if (key >= 32 && key <= 126) {
				if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
					delete_selection();
				}
				buffer_.insert(cursor_pos_, 1, static_cast<char>(key));
				cursor_pos_++;
				update_scroll();
				handled = true;
			}
		}
	} else if (ev.type == event_type::paste) {
		if (has_focus_) {
			if (selection_start_ != -1 && selection_end_ != -1 && selection_start_ != selection_end_) {
				delete_selection();
			}
			buffer_.insert(cursor_pos_, ev.payload);
			cursor_pos_ += ev.payload.length();
			update_scroll();
			handled = true;
		}
	}

	if (handled && buffer_ != orig_buffer && on_change_) {
		on_change_(buffer_);
	}
	return handled;
}