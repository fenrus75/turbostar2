#include "ui/components/ui_color_picker.h"
#include <map>
#include <utility>
#include <ncurses.h>

static int get_color_picker_pair(uint8_t fg, uint8_t bg)
{
	static std::map<std::pair<uint8_t, uint8_t>, int> allocated_pairs;
	static int next_pair = 200; // Start at 200 to avoid conflicting with terminal_window

	uint8_t f = fg & 0xF;
	uint8_t b = bg & 0xF;
	auto key = std::make_pair(f, b);
	auto it = allocated_pairs.find(key);
	if (it != allocated_pairs.end()) {
		return it->second;
	}

	int pair = next_pair++;
	if (pair < COLOR_PAIRS) {
		init_pair(pair, f, b);
	} else {
		pair = 0;
	}
	allocated_pairs[key] = pair;
	return pair;
}

ui_color_picker::ui_color_picker(std::string name, int x, int y, uint8_t initial_fg, uint8_t initial_bg, uint8_t dialog_bg, std::function<void(uint8_t, uint8_t)> on_change)
    : ui_element(std::move(name), x, y, 50, 6), selected_fg_(initial_fg), selected_bg_(initial_bg), dialog_bg_(dialog_bg), on_change_(std::move(on_change))
{
}

void ui_color_picker::draw(int abs_x, int abs_y) const
{
	// 1. Draw Foreground Label
	attrset(COLOR_PAIR(1));
	if (has_focus_ && !focus_bg_row_) {
		attron(A_BOLD);
		mvaddstr(abs_y, abs_x, "> Foreground Color:");
		attroff(A_BOLD);
	} else {
		mvaddstr(abs_y, abs_x, "  Foreground Color:");
	}

	// 2. Draw Foreground Blocks (16 colors, each 3 chars: " █ " or "[█]")
	for (uint8_t fg = 0; fg < 16; ++fg) {
		int block_x = abs_x + fg * 3;
		bool is_selected = (selected_fg_ == fg);

		// Draw left bracket/space
		attrset(COLOR_PAIR(1));
		if (is_selected) {
			if (has_focus_ && !focus_bg_row_) {
				attrset(COLOR_PAIR(19)); // Focused highlight color
			}
			mvaddch(abs_y + 1, block_x, '[');
		} else {
			mvaddch(abs_y + 1, block_x, ' ');
		}

		// Draw block char in target fg color
		int fg_pair = get_color_picker_pair(fg, dialog_bg_);
		attrset(COLOR_PAIR(fg_pair));
		addstr("█");

		// Draw right bracket/space
		attrset(COLOR_PAIR(1));
		if (is_selected) {
			if (has_focus_ && !focus_bg_row_) {
				attrset(COLOR_PAIR(19));
			}
			mvaddch(abs_y + 1, block_x + 2, ']');
		} else {
			mvaddch(abs_y + 1, block_x + 2, ' ');
		}
	}

	// 3. Draw Background Label
	attrset(COLOR_PAIR(1));
	if (has_focus_ && focus_bg_row_) {
		attron(A_BOLD);
		mvaddstr(abs_y + 2, abs_x, "> Background Color:");
		attroff(A_BOLD);
	} else {
		mvaddstr(abs_y + 2, abs_x, "  Background Color:");
	}

	// 4. Draw Background Blocks (8 colors, each 4 chars: " ██ " or "[██]")
	for (uint8_t bg = 0; bg < 8; ++bg) {
		int block_x = abs_x + bg * 4;
		bool is_selected = (selected_bg_ == bg);

		// Draw left bracket/space
		attrset(COLOR_PAIR(1));
		if (is_selected) {
			if (has_focus_ && focus_bg_row_) {
				attrset(COLOR_PAIR(19));
			}
			mvaddch(abs_y + 3, block_x, '[');
		} else {
			mvaddch(abs_y + 3, block_x, ' ');
		}

		// Draw background block
		int bg_pair = get_color_picker_pair(COLOR_WHITE, bg);
		attrset(COLOR_PAIR(bg_pair));
		addstr("  ");

		// Draw right bracket/space
		attrset(COLOR_PAIR(1));
		if (is_selected) {
			if (has_focus_ && focus_bg_row_) {
				attrset(COLOR_PAIR(19));
			}
			mvaddch(abs_y + 3, block_x + 3, ']');
		} else {
			mvaddch(abs_y + 3, block_x + 3, ' ');
		}
	}

	// 5. Draw Preview Label
	attrset(COLOR_PAIR(1));
	mvaddstr(abs_y + 4, abs_x, "  Preview:");

	// 6. Draw Preview Text Box
	int preview_pair = get_color_picker_pair(selected_fg_, selected_bg_);
	attrset(COLOR_PAIR(1));
	mvaddch(abs_y + 5, abs_x + 2, '[');
	attrset(COLOR_PAIR(preview_pair));
	addstr("  AaBbCcXxYyZz  ");
	attrset(COLOR_PAIR(1));
	addch(']');
}

bool ui_color_picker::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	bool handled = false;
	uint8_t old_fg = selected_fg_;
	uint8_t old_bg = selected_bg_;

	if (ev.type == event_type::key_press) {
		if (has_focus_) {
			if (ev.key_code == KEY_LEFT) {
				if (!focus_bg_row_) {
					selected_fg_ = (selected_fg_ + 15) % 16;
				} else {
					selected_bg_ = (selected_bg_ + 7) % 8;
				}
				handled = true;
			}
			else if (ev.key_code == KEY_RIGHT) {
				if (!focus_bg_row_) {
					selected_fg_ = (selected_fg_ + 1) % 16;
				} else {
					selected_bg_ = (selected_bg_ + 1) % 8;
				}
				handled = true;
			}
			else if (ev.key_code == KEY_UP) {
				if (focus_bg_row_) {
					focus_bg_row_ = false;
					handled = true;
				} else {
					// Propagate focus up
					ui_element *p = parent_;
					while (p) {
						if (p->focus_previous())
							break;
						p = p->parent();
					}
					handled = true;
				}
			}
			else if (ev.key_code == KEY_DOWN) {
				if (!focus_bg_row_) {
					focus_bg_row_ = true;
					handled = true;
				} else {
					// Propagate focus down
					ui_element *p = parent_;
					while (p) {
						if (p->focus_next())
							break;
						p = p->parent();
					}
					handled = true;
				}
			}
			else if (ev.key_code == '\t') {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_next())
						break;
					p = p->parent();
				}
				handled = true;
			}
			else if (ev.key_code == KEY_BTAB) {
				ui_element *p = parent_;
				while (p) {
					if (p->focus_previous())
						break;
					p = p->parent();
				}
				handled = true;
			}
		}
	}

	if (ev.type == event_type::mouse_click) {
		if (contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
			// Click on Foreground row
			if (ev.mouse_y == abs_y + 1) {
				int idx = (ev.mouse_x - abs_x) / 3;
				if (idx >= 0 && idx < 16) {
					selected_fg_ = idx;
					focus_bg_row_ = false;
					handled = true;
				}
			}
			// Click on Background row
			if (ev.mouse_y == abs_y + 3) {
				int idx = (ev.mouse_x - abs_x) / 4;
				if (idx >= 0 && idx < 8) {
					selected_bg_ = idx;
					focus_bg_row_ = true;
					handled = true;
				}
			}
		}
	}

	if (handled) {
		if (on_change_ && (selected_fg_ != old_fg || selected_bg_ != old_bg)) {
			on_change_(selected_fg_, selected_bg_);
		}
		return true;
	}

	return false;
}

std::optional<std::string> ui_color_picker::get_value(const std::string &target_name) const
{
	if (name_ == target_name) {
		return std::to_string(selected_fg_) + "," + std::to_string(selected_bg_);
	}
	return std::nullopt;
}
