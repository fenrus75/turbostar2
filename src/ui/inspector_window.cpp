#include "ui/inspector_window.h"
#include <algorithm>
#include <format>
#include <ncurses.h>

inspector_window::inspector_window(int id, int x, int y, int width, int height)
    : window(id, x, y, width, height, " Inspector ")
{
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

void inspector_window::draw_content(bool cursor_only) const
{
	if (cursor_only) {
		return;
	}

	// Fill background (pair 3: Dark Blue)
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
		attron(COLOR_PAIR(38)); // Dim unfocused text
		mvprintw(y_ + 1, x_ + 2, "%.*s", content_w - 2, "[No symbol under cursor]");
		attroff(COLOR_PAIR(38));
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	if (state_ == inspector_state::resolving) {
		attron(COLOR_PAIR(12)); // Bright white
		mvprintw(y_ + 1, x_ + 2, "%.*s", content_w - 2, token_.c_str());
		attroff(COLOR_PAIR(12));
		int token_len = static_cast<int>(token_.size());
		if (content_w - 4 - token_len > 0) {
			attron(COLOR_PAIR(38));
			mvprintw(y_ + 1, x_ + 2 + token_len + 1, "[resolving...]");
			attroff(COLOR_PAIR(38));
		}
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	// Active state:
	// Line 1: Signature or token
	int curr_y = y_ + 1;
	attron(COLOR_PAIR(32)); // Cyan label
	mvprintw(curr_y, x_ + 2, "Signature: ");
	attroff(COLOR_PAIR(32));
	int label_w = 11;
	attron(COLOR_PAIR(12)); // Bright white value
	std::string disp_sig = signature_.empty() ? token_ : signature_;
	if (content_w - label_w > 0) {
		mvprintw(curr_y, x_ + 2 + label_w, "%.*s", content_w - label_w - 2, disp_sig.c_str());
	}
	attroff(COLOR_PAIR(12));

	// Line 2: Proposals (call arguments) or location if proposals empty
	if (height_ >= 4) {
		curr_y++;
		if (!proposals_.empty()) {
			attron(COLOR_PAIR(32));
			mvprintw(curr_y, x_ + 2, "Args: ");
			attroff(COLOR_PAIR(32));
			int args_label_w = 6;
			std::string props_str;
			for (size_t i = 0; i < proposals_.size() && i < 3; ++i) {
				if (i > 0) {
					props_str += "  ";
				}
				props_str += std::format("{}. {}", i + 1, proposals_[i].call_text);
			}
			attron(COLOR_PAIR(3)); // Yellow
			if (content_w - args_label_w > 0) {
				mvprintw(curr_y, x_ + 2 + args_label_w, "%.*s", content_w - args_label_w - 2, props_str.c_str());
			}
			attroff(COLOR_PAIR(3));
		} else if (!location_.empty()) {
			attron(COLOR_PAIR(38));
			mvprintw(curr_y, x_ + 2, "Decl: %.*s", content_w - 8, location_.c_str());
			attroff(COLOR_PAIR(38));
		}
	}

	// Line 3 (if height >= 5): Declaration location
	if (height_ >= 5 && !proposals_.empty() && !location_.empty()) {
		curr_y++;
		attron(COLOR_PAIR(38));
		mvprintw(curr_y, x_ + 2, "Decl: %.*s", content_w - 8, location_.c_str());
		attroff(COLOR_PAIR(38));
	}

	attroff(COLOR_PAIR(background_color_pair_));
}
