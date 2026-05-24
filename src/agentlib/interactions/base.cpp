#include "base.h"
#include <algorithm>
#include <sstream>
#include "../../markdown_utils.h"

namespace agentlib
{

int agent_interaction::get_height(int width) const
{
	return render(width).size();
}

const std::vector<interaction_line> &agent_interaction::render(int width) const
{
	if (width != cached_width_) {
		if (!is_boxed_) {
			cached_lines_ = format_lines(width);
		} else {
			int inner_width = width - 4;
			if (inner_width < 10)
				inner_width = 10;

			std::vector<interaction_line> inner_lines = format_lines(inner_width);
			cached_lines_.clear();

			// Single line box drawing characters
			std::string top_left = "\xE2\x94\x8C";
			std::string horiz = "\xE2\x94\x80";
			std::string top_right = "\xE2\x94\x90";
			std::string vert = "\xE2\x94\x82";
			std::string bot_left = "\xE2\x94\x94";
			std::string bot_right = "\xE2\x94\x98";

			std::string top_border = top_left;

			if (!box_title_.empty()) {
				std::string title_str = " " + box_title_ + " ";
				int title_len = markdown_utils::utf8_length(title_str);
				int border_len = inner_width + 2;

				if (title_len >= border_len) {
					// Title is too long, just draw horizontal line
					for (int i = 0; i < border_len; ++i)
						top_border += horiz;
				} else {
					int left_pad = (border_len - title_len) / 2;
					int right_pad = border_len - title_len - left_pad;

					for (int i = 0; i < left_pad; ++i)
						top_border += horiz;
					top_border += title_str;
					for (int i = 0; i < right_pad; ++i)
						top_border += horiz;
				}
			} else {
				for (int i = 0; i < inner_width + 2; ++i)
					top_border += horiz;
			}

			top_border += top_right;

			interaction_line top_line;
			top_line.text = top_border;
			top_line.color_pair = box_color_pair_;
			cached_lines_.push_back(top_line);

			for (const auto &line : inner_lines) {
				int content_len = markdown_utils::utf8_length(line.text);
				int pad_len = inner_width - content_len;
				if (pad_len < 0)
					pad_len = 0;

				interaction_line boxed_line = line;
				boxed_line.prefix = vert + " ";
				boxed_line.prefix_color_pair = box_color_pair_;
				boxed_line.suffix = std::string(pad_len, ' ') + " " + vert;
				boxed_line.suffix_color_pair = box_color_pair_;

				cached_lines_.push_back(boxed_line);
			}

			std::string bot_border = bot_left;
			for (int i = 0; i < inner_width + 2; ++i)
				bot_border += horiz;
			bot_border += bot_right;

			interaction_line bot_line;
			bot_line.text = bot_border;
			bot_line.color_pair = box_color_pair_;
			cached_lines_.push_back(bot_line);
		}
		cached_width_ = width;
	}
	return cached_lines_;
}

std::vector<interaction_line> agent_interaction::wrap_text(const std::string &prefix, const std::string &text, int width, int color_pair)
{
	std::vector<interaction_line> lines;
	int prefix_utf8_len = markdown_utils::utf8_length(prefix);

	std::string full_text;
	size_t line_chars = 0;
	for (size_t i = 0; i < text.length();) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '\n') {
			full_text += c;
			line_chars = 0;
			i++;
		} else if (c == '\t') {
			int spaces = 4 - (line_chars % 4);
			full_text.append(spaces, ' ');
			line_chars += spaces;
			i++;
		} else {
			size_t char_bytes = 1;
			if ((c & 0xE0) == 0xC0)
				char_bytes = 2;
			else if ((c & 0xF0) == 0xE0)
				char_bytes = 3;
			else if ((c & 0xF8) == 0xF0)
				char_bytes = 4;
			if (i + char_bytes > text.length())
				char_bytes = text.length() - i;
			full_text.append(text, i, char_bytes);
			line_chars++;
			i += char_bytes;
		}
	}

	if (width <= prefix_utf8_len + 5) {
		lines.push_back({prefix + full_text, color_pair});
		return lines;
	}

	std::stringstream ss(full_text);
	std::string line;
	bool first = true;

	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();

		std::string current_prefix = first ? prefix : std::string(prefix_utf8_len, ' ');
		int available_width = width - prefix_utf8_len;

		if (line.empty()) {
			lines.push_back({current_prefix, color_pair});
			first = false;
			continue;
		}

		size_t byte_idx = 0;
		while (byte_idx < line.length()) {
			// Find how many bytes we can consume to fit within available_width characters
			size_t chunk_byte_len = 0;
			int chars_consumed = 0;
			size_t last_space_byte_idx = std::string::npos;
			size_t last_space_chars = 0;

			size_t peek_idx = byte_idx;
			while (peek_idx < line.length() && chars_consumed < available_width) {
				unsigned char c = static_cast<unsigned char>(line[peek_idx]);
				size_t char_bytes = 1;
				if ((c & 0xE0) == 0xC0)
					char_bytes = 2;
				else if ((c & 0xF0) == 0xE0)
					char_bytes = 3;
				else if ((c & 0xF8) == 0xF0)
					char_bytes = 4;

				if (peek_idx + char_bytes > line.length()) {
					char_bytes = line.length() - peek_idx; // Malformed UTF-8 fallback
				}

				if (c == ' ' || c == '\t') {
					last_space_byte_idx = peek_idx;
					last_space_chars = chars_consumed;
				}

				peek_idx += char_bytes;
				chars_consumed++;
				chunk_byte_len += char_bytes;
			}

			// If we hit the width limit, but the line continues, try to break at the last space
			if (peek_idx < line.length() && last_space_byte_idx != std::string::npos && last_space_byte_idx > byte_idx) {
				chunk_byte_len = last_space_byte_idx - byte_idx;
				chars_consumed = last_space_chars;
			}

			if (chunk_byte_len == 0)
				break; // Safety net

			lines.push_back({current_prefix + line.substr(byte_idx, chunk_byte_len), color_pair});

			byte_idx += chunk_byte_len;

			// Skip trailing spaces for the next wrapped line
			while (byte_idx < line.length() && (line[byte_idx] == ' ' || line[byte_idx] == '\t')) {
				byte_idx++;
			}

			current_prefix = std::string(prefix_utf8_len, ' ');
			first = false;
		}
	}

	if (lines.empty()) {
		lines.push_back({prefix, color_pair});
	}

	return lines;
}

bool agent_interaction::can_merge_with_previous(const agent_interaction &previous) const
{
	interaction_type my_type = get_type();
	interaction_type prev_type = previous.get_type();

	// System messages only merge with each other
	if (my_type == interaction_type::system_message) {
		return prev_type == interaction_type::system_message;
	}

	// User messages only merge with each other
	if (my_type == interaction_type::user_message) {
		return prev_type == interaction_type::user_message;
	}

	// Everything else merges into the current turn, provided the current turn
	// isn't a system message block.
	return prev_type != interaction_type::system_message;
}

} // namespace agentlib
