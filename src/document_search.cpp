#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <re2/re2.h>
#include <re2/stringpiece.h>
#include "config_manager.h"
#include "document.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "lsp_manager.h"

namespace fs = std::filesystem;

bool document::find_next(const search_params &params, bool is_repeat)
{
	if (params.query.empty())
		return false;

	re2::RE2::Options options;
	options.set_log_errors(false);
	if (params.ignore_case)
		options.set_case_sensitive(false);

	std::string pattern = params.query;
	if (params.whole_words && !params.regex) {
		pattern = "\\b" + pattern + "\\b";
	}

	// If literal search, escape it for RE2
	if (!params.regex && !params.whole_words) {
		pattern = re2::RE2::QuoteMeta(pattern);
	}

	re2::RE2 re(pattern, options);
	if (!re.ok())
		return false;

	std::unique_lock lock(mutex_);
	int start_y = cursor_y_;
	int start_x = cursor_x_;

	int scope_sy = 0, scope_sx = 0, scope_ey = line_count_unlocked() - 1, scope_ex = lines_.back()->length_in_chars();
	if (params.selected_text_only && selection_start_y_ != -1) {
		get_selection_range(scope_sx, scope_sy, scope_ex, scope_ey);
	}

	if (!params.from_cursor) {
		if (params.backward) {
			start_y = scope_ey;
			start_x = scope_ex;
		} else {
			start_y = scope_sy;
			start_x = scope_sx;
		}
	} else if (is_repeat) {
		// Step over current char
		if (params.backward) {
			if (start_x > 0)
				start_x--;
			else if (start_y > scope_sy) {
				start_y--;
				start_x = lines_[start_y]->length_in_chars();
			} else
				return false;
		} else {
			if (start_x < lines_[start_y]->length_in_chars())
				start_x++;
			else if (start_y < scope_ey) {
				start_y++;
				start_x = 0;
			} else
				return false;
		}
	}

	auto check_line = [&](int y, int x_limit) -> int {
		std::string line_text = lines_[y]->get_text();
		std::string original_line_text = line_text;

		int best_found_char_idx = -1;
		size_t byte_limit = lines_[y]->char_to_byte_offset(x_limit);

		size_t line_scope_start_byte = 0;
		size_t line_scope_end_byte = line_text.length();
		if (params.selected_text_only) {
			if (y == scope_sy)
				line_scope_start_byte = lines_[y]->char_to_byte_offset(scope_sx);
			if (y == scope_ey)
				line_scope_end_byte = lines_[y]->char_to_byte_offset(scope_ex);
		}

		re2::StringPiece input(line_text);
		re2::StringPiece match;
		size_t search_start = 0;
		while (re.Match(input, search_start, input.size(), re2::RE2::UNANCHORED, &match, 1)) {
			search_start = (match.data() - input.data()) + match.size();
			if (match.size() == 0)
				search_start++; // prevent infinite loop

			size_t byte_pos = match.data() - line_text.data();

			if (params.backward) {
				if (byte_pos >= line_scope_start_byte && byte_pos <= byte_limit) {
					int found_char_idx = 0;
					size_t current_byte = 0;
					while (current_byte < byte_pos && current_byte < original_line_text.length()) {
						unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
						if (c < 0x80)
							current_byte += 1;
						else if ((c & 0xE0) == 0xC0)
							current_byte += 2;
						else if ((c & 0xE0) == 0xE0)
							current_byte += 3;
						else if ((c & 0xF0) == 0xF0)
							current_byte += 4;
						else
							current_byte += 1;
						found_char_idx++;
					}
					best_found_char_idx = found_char_idx;
				}
			} else {
				if (byte_pos >= byte_limit && byte_pos < line_scope_end_byte) {
					int found_char_idx = 0;
					size_t current_byte = 0;
					while (current_byte < byte_pos && current_byte < original_line_text.length()) {
						unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
						if (c < 0x80)
							current_byte += 1;
						else if ((c & 0xE0) == 0xC0)
							current_byte += 2;
						else if ((c & 0xE0) == 0xE0)
							current_byte += 3;
						else if ((c & 0xF0) == 0xF0)
							current_byte += 4;
						else
							current_byte += 1;
						found_char_idx++;
					}
					return found_char_idx;
				}
			}
		}
		return best_found_char_idx;
	};

	if (params.backward) {
		for (int y = start_y; y >= scope_sy; --y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = lines_[y]->length_in_chars();
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				notify_cursor_changed();
				return true;
			}
		}
	} else {
		for (int y = start_y; y <= scope_ey; ++y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = 0;
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				notify_cursor_changed();
				return true;
			}
		}
	}

	lock.unlock();
	notify_cursor_changed();
	return false;
}

std::optional<std::pair<int, int>> document::find_matching_bracket(int start_y, int start_x) const
{
	std::shared_lock lock(mutex_);
	if (start_y < 0 || start_y >= line_count_unlocked())
		return std::nullopt;

	std::string text = lines_[start_y]->get_text();
	if (start_x < 0 || start_x >= static_cast<int>(text.length()))
		return std::nullopt;

	char start_char = text[start_x];
	char target_char = 0;
	bool forward = true;

	if (start_char == '(') {
		target_char = ')';
		forward = true;
	} else if (start_char == '[') {
		target_char = ']';
		forward = true;
	} else if (start_char == '{') {
		target_char = '}';
		forward = true;
	} else if (start_char == ')') {
		target_char = '(';
		forward = false;
	} else if (start_char == ']') {
		target_char = '[';
		forward = false;
	} else if (start_char == '}') {
		target_char = '{';
		forward = false;
	} else
		return std::nullopt;

	int depth = 0;
	if (forward) {
		for (int y = start_y; y < line_count_unlocked(); ++y) {
			std::string l_text = lines_[y]->get_text();
			for (int x = (y == start_y ? start_x : 0); x < static_cast<int>(l_text.length()); ++x) {
				if (l_text[x] == start_char)
					depth++;
				else if (l_text[x] == target_char)
					depth--;

				if (depth == 0)
					return std::make_pair(y, x);
			}
		}
	} else {
		for (int y = start_y; y >= 0; --y) {
			std::string l_text = lines_[y]->get_text();
			for (int x = (y == start_y ? start_x : static_cast<int>(l_text.length()) - 1); x >= 0; --x) {
				if (l_text[x] == start_char)
					depth++;
				else if (l_text[x] == target_char)
					depth--;

				if (depth == 0)
					return std::make_pair(y, x);
			}
		}
	}

	return std::nullopt;
}

void document::select_enclosing_scope()
{
	std::shared_lock lock(mutex_);
	int sy = cursor_y_;
	int sx = cursor_x_;

	while (sy >= 0) {
		std::string text = lines_[sy]->get_text();
		int start_x = (sy == cursor_y_) ? std::min(sx, static_cast<int>(text.length()) - 1) : static_cast<int>(text.length()) - 1;

		for (int x = start_x; x >= 0; --x) {
			if (text[x] == '{') {
				// Potential start. Find match.
				lock.unlock();
				auto match = find_matching_bracket(sy, x);
				lock.lock();

				if (match) {
					// Check if cursor is inside (or on the boundaries)
					bool is_inside = false;
					if (match->first > cursor_y_ || (match->first == cursor_y_ && match->second >= cursor_x_)) {
						is_inside = true;
					}

					if (is_inside) {
						lock.unlock();
						std::unique_lock ulock(mutex_);
						selection_start_y_ = sy;
						selection_start_x_ = x;
						selection_end_y_ = match->first;
						selection_end_x_ = match->second + 1; // Include the closing brace
						ulock.unlock();
						notify_cursor_changed();
						return;
					}
				}
			}
		}
		sy--;
	}
}
