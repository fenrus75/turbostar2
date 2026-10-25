#include "ui/inspector_window.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <ncurses.h>
#include "ui/ui_element.h"

inspector_window::inspector_window(int id, int x, int y, int width, int height)
    : window(id, x, y, width, height, "TurboStar Inspector")
{
	background_color_pair_ = 17; // Borland Cyan background (Pair 17: Black on Cyan)
	set_bypass_global_shortcuts(true);
	set_visible(false);
	set_idle();
}

void inspector_window::set_markdown(std::string_view markdown, int score, std::string_view provider_name,
				    std::string_view title_addon)
{
	markdown_ = std::string(markdown);
	score_ = score;
	provider_name_ = std::string(provider_name);
	title_addon_ = std::string(title_addon);

	if (markdown_.empty() || score <= 0) {
		state_ = inspector_state::idle;
	} else if (state_ != inspector_state::blanked) {
		state_ = inspector_state::active;
	}
	invalidate();
}

void inspector_window::set_idle()
{
	token_.clear();
	signature_.clear();
	proposals_.clear();
	location_.clear();
	title_addon_.clear();
	set_markdown("*[No symbol under cursor]*", 0, "");
	state_ = inspector_state::idle;
}

void inspector_window::set_resolving(const std::string &token)
{
	token_ = token;
	signature_.clear();
	proposals_.clear();
	location_.clear();
	set_markdown(std::format("`{}` *[resolving...]*", token), 10, "lsp");
	state_ = inspector_state::resolving;
}

void inspector_window::set_active_info(const std::string &token, const std::string &signature,
				       const std::vector<turbostar::call_suggestion> &proposals,
				       const std::string &location)
{
	token_ = token;
	signature_ = signature;
	proposals_ = proposals;
	location_ = location;

	std::string md;
	std::string disp_sig = signature.empty() ? token : signature;
	md += std::format("**Signature:** `{}`\n", disp_sig);

	if (!proposals.empty()) {
		md += "**Args:** ";
		for (size_t i = 0; i < proposals.size() && i < 3; ++i) {
			if (i > 0) {
				md += "  ";
			}
			md += std::format("{}. `{}`", i + 1, proposals[i].call_text);
		}
		md += "\n";
	}

	if (!location.empty()) {
		md += std::format("**Decl:** `{}`\n", location);
	}

	set_markdown(md, 90, "lsp");
	state_ = inspector_state::active;
}

void inspector_window::set_blanked()
{
	state_ = inspector_state::blanked;
	invalidate();
}

std::vector<markdown_token> inspector_window::parse_markdown_line(std::string_view line)
{
	std::vector<markdown_token> tokens;
	if (line.empty()) {
		return tokens;
	}

	size_t i = 0;
	bool in_bold = false;
	bool in_italic = false;
	bool in_code = false;
	std::string current;

	auto flush = [&](int override_pair = -1, int override_attr = -1) {
		if (current.empty()) {
			return;
		}
		markdown_token tok;
		tok.text = current;
		if (override_pair != -1) {
			tok.color_pair = override_pair;
			tok.attr = override_attr != -1 ? override_attr : 0;
		} else if (in_bold) {
			tok.color_pair = 8;
			tok.attr = A_BOLD;
		} else if (in_italic) {
			if (current.find("resolving") != std::string::npos) {
				tok.color_pair = 18;
				tok.attr = A_BOLD;
			} else {
				tok.color_pair = 8;
				tok.attr = A_DIM;
			}
		} else if (in_code) {
			tok.color_pair = 17;
			tok.attr = 0;
		} else {
			tok.color_pair = 17;
			tok.attr = 0;
		}
		tokens.push_back(std::move(tok));
		current.clear();
	};

	while (i < line.size()) {
		// Bold: ** or __
		if (!in_code && i + 1 < line.size() &&
		    ((line[i] == '*' && line[i + 1] == '*') || (line[i] == '_' && line[i + 1] == '_'))) {
			flush();
			in_bold = !in_bold;
			i += 2;
			continue;
		}

		// Inline code: `
		if (line[i] == '`') {
			flush();
			in_code = !in_code;
			i += 1;
			continue;
		}

		// Italic: * or _ (when not bold)
		if (!in_code && !in_bold && (line[i] == '*' || line[i] == '_')) {
			flush();
			in_italic = !in_italic;
			i += 1;
			continue;
		}

		// Numbered list item: e.g. "1. " or "2. " (at start of line or preceded by space)
		if (!in_code && !in_bold && !in_italic) {
			bool at_boundary = (i == 0 || line[i - 1] == ' ');
			if (at_boundary && std::isdigit(static_cast<unsigned char>(line[i]))) {
				size_t j = i;
				while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) {
					j++;
				}
				if (j < line.size() && line[j] == '.' &&
				    (j + 1 == line.size() || line[j + 1] == ' ')) {
					flush();
					current = std::string(line.substr(i, j - i + 1));
					flush(18, A_BOLD); // Bright Yellow
					i = j + 1;
					continue;
				}
			} else if (at_boundary && (line[i] == '-' || line[i] == '*') &&
				   (i + 1 < line.size() && line[i + 1] == ' ')) {
				flush();
				current = std::string(line.substr(i, 1));
				flush(18, A_BOLD); // Bright Yellow bullet
				i += 1;
				continue;
			}
		}

		// Link syntax: [text](url) -> render 'text' highlighted
		if (!in_code && line[i] == '[') {
			size_t close_bracket = line.find(']', i + 1);
			if (close_bracket != std::string_view::npos && close_bracket + 1 < line.size() &&
			    line[close_bracket + 1] == '(') {
				size_t close_paren = line.find(')', close_bracket + 2);
				if (close_paren != std::string_view::npos) {
					flush();
					current = std::string(line.substr(i + 1, close_bracket - (i + 1)));
					flush(8, A_BOLD);
					i = close_paren + 1;
					continue;
				}
			}
		}

		current += line[i];
		i++;
	}

	flush();
	return tokens;
}

void inspector_window::draw_border() const
{
	int border_pair = 8; // Bright white on cyan double-line border
	int title_pair = 8;  // Bright white on cyan title

	ui_utils::draw_border(x_, y_, width_, height_, ui_utils::border_style::double_line, border_pair);

	std::string current_title = get_displayed_title();
	if (current_title.empty() || current_title == "TurboStar Inspector") {
		if (!provider_name_.empty()) {
			if (!title_addon_.empty()) {
				current_title = std::format("Inspector: {} ({}) - {}", provider_name_, score_, title_addon_);
			} else {
				current_title = std::format("Inspector: {} ({})", provider_name_, score_);
			}
		} else {
			current_title = "TurboStar Inspector";
		}
	}
	if (!current_title.empty()) {
		int max_title_len = std::max(0, width_ - 8);
		if (static_cast<int>(current_title.length()) > max_title_len && max_title_len > 3) {
			current_title = current_title.substr(0, max_title_len - 3) + "...";
		}
		int title_x = x_ + (width_ - static_cast<int>(current_title.length())) / 2;
		attron(COLOR_PAIR(title_pair) | A_BOLD);
		mvprintw(y_, title_x - 1, " %s ", current_title.c_str());
		attroff(COLOR_PAIR(title_pair) | A_BOLD);
	}

	// Close widget [■]
	attron(COLOR_PAIR(border_pair));
	mvaddstr(y_, x_ + 2, "[");
	attron(COLOR_PAIR(18) | A_BOLD); // Bright yellow on cyan
	addstr("■");
	attroff(COLOR_PAIR(18) | A_BOLD);
	attron(COLOR_PAIR(border_pair));
	addstr("]");
	attroff(COLOR_PAIR(border_pair));
}

void inspector_window::draw_content(bool cursor_only) const
{
	if (cursor_only) {
		return;
	}

	// Fill background (Pair 17: Black on Cyan)
	attron(COLOR_PAIR(background_color_pair_));
	for (int row = y_ + 1; row < y_ + height_ - 1; ++row) {
		mvhline(row, x_ + 1, ' ', width_ - 2);
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

	if (markdown_.empty()) {
		attroff(COLOR_PAIR(background_color_pair_));
		return;
	}

	// Split markdown into lines
	std::vector<std::string_view> lines;
	std::string_view remaining = markdown_;
	while (!remaining.empty()) {
		size_t pos = remaining.find('\n');
		if (pos == std::string_view::npos) {
			lines.push_back(remaining);
			break;
		}
		lines.push_back(remaining.substr(0, pos));
		remaining.remove_prefix(pos + 1);
	}

	int curr_y = y_ + 1;
	int max_y = y_ + height_ - 2;

	for (const auto &raw_line : lines) {
		if (curr_y > max_y) {
			break;
		}

		auto tokens = parse_markdown_line(raw_line);
		int cur_col = x_ + 2;
		int max_col = x_ + content_w;

		for (const auto &tok : tokens) {
			if (cur_col >= max_col) {
				break;
			}
			int rem_w = max_col - cur_col;
			int tok_len = static_cast<int>(tok.text.size());

			attron(COLOR_PAIR(tok.color_pair) | tok.attr);
			if (tok_len <= rem_w) {
				mvaddstr(curr_y, cur_col, tok.text.c_str());
				cur_col += tok_len;
			} else {
				if (rem_w > 3) {
					mvprintw(curr_y, cur_col, "%.*s...", rem_w - 3, tok.text.c_str());
				} else {
					mvprintw(curr_y, cur_col, "%.*s", rem_w, tok.text.c_str());
				}
				cur_col = max_col;
			}
			attroff(COLOR_PAIR(tok.color_pair) | tok.attr);
		}

		curr_y++;
	}

	attroff(COLOR_PAIR(background_color_pair_));
}
