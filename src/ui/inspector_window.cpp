#include "ui/inspector_window.h"
#include <algorithm>
#include <format>
#include <ncurses.h>
#include "ui/ui_element.h"

inspector_window::inspector_window(int id, int x, int y, int width, int height)
    : window(id, x, y, width, height, " Inspector ")
{
	background_color_pair_ = 1; // Light Gray background (Pair 1: Black on Light Gray)
	set_bypass_global_shortcuts(true);
}

void inspector_window::set_idle()
{
	state_ = inspector_state::idle;
	token_.clear();
	signature_.clear();
	proposals_.clear();
	location_.clear();
	invalidate();
}

void inspector_window::set_resolving(const std::string &token)
{
	state_ = inspector_state::resolving;
	token_ = token;
	signature_.clear();
	proposals_.clear();
	location_.clear();
	invalidate();
}

void inspector_window::set_active_info(const std::string &token, const std::string &signature,
				       const std::vector<turbostar::call_suggestion> &proposals,
				       const std::string &location)
{
	state_ = inspector_state::active;
	token_ = token;
	signature_ = signature;
	proposals_ = proposals;
	location_ = location;
	invalidate();
}

void inspector_window::set_blanked()
{
	state_ = inspector_state::blanked;
	invalidate();
}

void inspector_window::draw_border() const
{
	int border_pair = 11; // Bright white on light gray
	int title_pair = 1;   // Black on light gray

	ui_utils::draw_border(x_, y_, width_, height_, ui_utils::border_style::double_line, border_pair);

	std::string current_title = get_displayed_title();
	if (!current_title.empty()) {
		int title_x = x_ + (width_ - static_cast<int>(current_title.length())) / 2;
		attron(COLOR_PAIR(title_pair) | A_BOLD);
		mvprintw(y_, title_x - 1, " %s ", current_title.c_str());
		attroff(COLOR_PAIR(title_pair) | A_BOLD);
	}

	// Close widget [■]
	attron(COLOR_PAIR(border_pair));
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(2)); // Red on light gray
	addstr("■");
	attroff(COLOR_PAIR(2));
	attron(COLOR_PAIR(border_pair));
	addstr("]");
	attroff(COLOR_PAIR(border_pair));
}

void inspector_window::draw_content(bool cursor_only) const
{
	if (cursor_only) {
		return;
	}

	// Fill background (Pair 1: Black on Light Gray)
	attron(COLOR_PAIR(background_color_pair_));
	for (int i = 1; i < height_ - 1; ++i) {
		mvhline(y_ + i, x_ + 1, ' ', width_ - 2);
	}

	if (state_ == inspector_state::blanked) {
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	int content_w = width_ - 2;
	if (content_w <= 0) {
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	if (state_ == inspector_state::idle) {
		attron(COLOR_PAIR(37)); // Dark gray on light gray
		mvprintw(y_ + 1, x_ + 2, "%.*s", content_w - 2, "[No symbol under cursor]");
		attroff(COLOR_PAIR(37));
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	if (state_ == inspector_state::resolving) {
		attron(COLOR_PAIR(1) | A_BOLD); // Black on light gray
		mvprintw(y_ + 1, x_ + 2, "%.*s", content_w - 2, token_.c_str());
		attroff(COLOR_PAIR(1) | A_BOLD);
		int token_len = static_cast<int>(token_.size());
		if (content_w - 4 - token_len > 0) {
			attron(COLOR_PAIR(37)); // Dark gray on light gray
			mvprintw(y_ + 1, x_ + 2 + token_len + 1, "[resolving...]");
			attroff(COLOR_PAIR(37));
		}
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	// Active state:
	// Line 1: Signature or token
	int curr_y = y_ + 1;
	attron(COLOR_PAIR(43) | A_BOLD); // Blue on light gray
	mvprintw(curr_y, x_ + 2, "Signature: ");
	attroff(COLOR_PAIR(43) | A_BOLD);
	int label_w = 11;
	attron(COLOR_PAIR(1)); // Black on light gray
	std::string disp_sig = signature_.empty() ? token_ : signature_;
	if (content_w - label_w > 0) {
		mvprintw(curr_y, x_ + 2 + label_w, "%.*s", content_w - label_w - 2, disp_sig.c_str());
	}
	attroff(COLOR_PAIR(1));

	// Line 2: Proposals (call arguments) or location if proposals empty
	if (height_ >= 4) {
		curr_y++;
		if (!proposals_.empty()) {
			attron(COLOR_PAIR(43) | A_BOLD); // Blue on light gray
			mvprintw(curr_y, x_ + 2, "Args: ");
			attroff(COLOR_PAIR(43) | A_BOLD);
			int args_label_w = 6;
			int cur_col = x_ + 2 + args_label_w;
			for (size_t i = 0; i < proposals_.size() && i < 3; ++i) {
				if (cur_col >= x_ + content_w) {
					break;
				}
				if (i > 0) {
					attron(COLOR_PAIR(1));
					mvaddstr(curr_y, cur_col, "  ");
					attroff(COLOR_PAIR(1));
					cur_col += 2;
				}
				std::string num_str = std::format("{}.", i + 1);
				attron(COLOR_PAIR(2) | A_BOLD); // Red on light gray
				mvaddstr(curr_y, cur_col, num_str.c_str());
				attroff(COLOR_PAIR(2) | A_BOLD);
				cur_col += static_cast<int>(num_str.size());

				attron(COLOR_PAIR(1)); // Black on light gray
				mvaddch(curr_y, cur_col, ' ');
				cur_col++;
				int rem_w = std::max(0, (x_ + content_w) - cur_col);
				mvprintw(curr_y, cur_col, "%.*s", rem_w, proposals_[i].call_text.c_str());
				attroff(COLOR_PAIR(1));
				cur_col += std::min(rem_w, static_cast<int>(proposals_[i].call_text.size()));
			}
		} else if (!location_.empty()) {
			attron(COLOR_PAIR(37)); // Dark gray on light gray
			mvprintw(curr_y, x_ + 2, "Decl: %.*s", content_w - 8, location_.c_str());
			attroff(COLOR_PAIR(37));
		}
	}

	// Line 3 (if height >= 5): Declaration location
	if (height_ >= 5 && !proposals_.empty() && !location_.empty()) {
		curr_y++;
		attron(COLOR_PAIR(37)); // Dark gray on light gray
		mvprintw(curr_y, x_ + 2, "Decl: %.*s", content_w - 8, location_.c_str());
		attroff(COLOR_PAIR(37));
	}

	attroff(COLOR_PAIR(background_color_pair_));
}
