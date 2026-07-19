#include "base.h"
#include <algorithm>
#include <sstream>
#include "../../markdown_utils.h"
#include "../../utf8.h"
#include "highlighter/highlighter_registry.h"
#include "syntax_color_manager.h"
#include "line.h"

namespace agentlib
{

constexpr int kBoxPadding = 4;
constexpr int kMinInnerWidth = 10;
constexpr int kEarlyReturnMargin = 5;

int agent_interaction::get_height(int width) const
{
	return render(width).size();
}

const std::vector<interaction_line> &agent_interaction::render(int width, background_mode bg) const
{
	if (width != cached_width_ || bg != cached_bg_) {
		if (!is_boxed_) {
			cached_lines_ = format_lines(width, bg);
		} else {
			int inner_width = width - kBoxPadding;
			if (inner_width < kMinInnerWidth)
				inner_width = kMinInnerWidth;

			std::vector<interaction_line> inner_lines = format_lines(inner_width, bg);
			cached_lines_.clear();

			// Single line box drawing characters
			const std::string top_left = "╭";  // U+250C
			const std::string horiz = "─";	   // U+2500
			const std::string top_right = "╮"; // U+2510
			const std::string vert = "│";	   // U+2502
			const std::string bot_left = "╰";  // U+2514
			const std::string bot_right = "╯"; // U+2518

			std::string top_border = top_left;
			int box_cp = get_color_pair(get_role(), bg);

			if (!box_title_.empty()) {
				std::string title_str = " " + box_title_ + " ";
				int title_len = markdown_utils::display_width(title_str);
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
			top_line.color_pair = box_cp;
			cached_lines_.push_back(top_line);

			for (const auto &line : inner_lines) {
				int content_len = markdown_utils::display_width(line.text);
				int pad_len = inner_width - content_len;
				if (pad_len < 0)
					pad_len = 0;

				interaction_line boxed_line = line;
				boxed_line.prefix = vert + " ";
				boxed_line.prefix_color_pair = box_cp;
				boxed_line.suffix = std::string(pad_len, ' ') + " " + vert;
				boxed_line.suffix_color_pair = box_cp;

				cached_lines_.push_back(boxed_line);
			}

			std::string bot_border = bot_left;
			for (int i = 0; i < inner_width + 2; ++i)
				bot_border += horiz;
			bot_border += bot_right;

			interaction_line bot_line;
			bot_line.text = bot_border;
			bot_line.color_pair = box_cp;
			cached_lines_.push_back(bot_line);
		}
		cached_width_ = width;
		cached_bg_ = bg;
	}
	return cached_lines_;
}

std::vector<interaction_line> agent_interaction::wrap_text(const std::string &prefix, const std::string &text, int width, int color_pair)
{
	std::vector<interaction_line> lines;
	int prefix_utf8_len = markdown_utils::display_width(prefix);

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
			size_t char_bytes = utf8::char_len(c);
			if (i + char_bytes > text.length())
				char_bytes = text.length() - i;
			full_text.append(text, i, char_bytes);
			line_chars++;
			i += char_bytes;
		}
	}

	if (width <= prefix_utf8_len + kEarlyReturnMargin) {
		lines.push_back({prefix + full_text, color_pair});
		return lines;
	}

	std::stringstream ss(full_text);
	std::string line_str;
	bool first = true;
	bool in_code_block = false;
	std::shared_ptr<syntax_highlighter> active_highlighter = nullptr;

	int code_color = 3;

	while (std::getline(ss, line_str)) {
		if (!line_str.empty() && line_str.back() == '\r')
			line_str.pop_back();

		if (line_str.starts_with("```")) {
			in_code_block = !in_code_block;
			if (in_code_block) {
				std::string lang = line_str.substr(3);
				while (!lang.empty() && std::isspace(static_cast<unsigned char>(lang.back()))) {
					lang.pop_back();
				}
				for (char &ch : lang) {
					ch = std::tolower(static_cast<unsigned char>(ch));
				}
				active_highlighter = highlighter_registry::get_instance().get_highlighter_for_language(lang);
			} else {
				active_highlighter = nullptr;
			}
		}

		int current_color = in_code_block ? code_color : color_pair;
		if (!in_code_block && line_str.starts_with("```")) {
			current_color = code_color; // Color the closing backticks as code too
		}

		std::vector<syntax_attribute> line_attrs;
		if (in_code_block && active_highlighter && !line_str.starts_with("```")) {
			auto temp_line = std::make_shared<::line>(line_str);
			active_highlighter->highlight(temp_line);
			std::string out_text;
			temp_line->get_content(out_text, line_attrs);
		}

		std::vector<rich_char> line_glyphs;
		if (in_code_block || line_str.starts_with("```")) {
			size_t byte_idx = 0;
			while (byte_idx < line_str.length()) {
				unsigned char c = static_cast<unsigned char>(line_str[byte_idx]);
				size_t char_bytes = utf8::char_len(c);
				if (byte_idx + char_bytes > line_str.length()) {
					char_bytes = line_str.length() - byte_idx;
				}
				rich_char rc;
				rc.ch = line_str.substr(byte_idx, char_bytes);
				rc.color_pair = current_color;
				rc.attr = 0;
				line_glyphs.push_back(rc);
				byte_idx += char_bytes;
			}
		} else {
			bool is_bold = false;
			bool is_inline_code = false;

			size_t byte_idx = 0;
			while (byte_idx < line_str.length()) {
				if (line_str[byte_idx] == '|' && (byte_idx == 0 || line_str[byte_idx - 1] != '\\')) {
					is_bold = false;
					is_inline_code = false;
				}

				if (!is_inline_code && byte_idx + 1 < line_str.length() && line_str.substr(byte_idx, 2) == "**") {
					is_bold = !is_bold;
					byte_idx += 2;
					continue;
				}

				if (line_str[byte_idx] == '`') {
					is_inline_code = !is_inline_code;
					byte_idx += 1;
					continue;
				}

				unsigned char c = static_cast<unsigned char>(line_str[byte_idx]);
				size_t char_bytes = utf8::char_len(c);
				if (byte_idx + char_bytes > line_str.length()) {
					char_bytes = line_str.length() - byte_idx;
				}

				rich_char rc;
				rc.ch = line_str.substr(byte_idx, char_bytes);
				rc.color_pair = is_inline_code ? code_color : color_pair;
				rc.attr = 0;
				if (is_bold) rc.attr |= ATTR_BOLD;

				line_glyphs.push_back(rc);
				byte_idx += char_bytes;
			}
		}

		std::string current_prefix = first ? prefix : std::string(prefix_utf8_len, ' ');
		int available_width = width - prefix_utf8_len;

		if (line_glyphs.empty()) {
			interaction_line new_line_item;
			new_line_item.text = current_prefix;
			new_line_item.color_pair = current_color;
			lines.push_back(new_line_item);
			first = false;
			continue;
		}

		size_t glyph_idx = 0;
		while (glyph_idx < line_glyphs.size()) {
			size_t chunk_len = 0;
			int display_width_consumed = 0;
			size_t last_space_idx = std::string::npos;
			size_t last_space_display_w = 0;

			size_t peek_idx = glyph_idx;
			while (peek_idx < line_glyphs.size() && display_width_consumed < available_width) {
				const auto &rg = line_glyphs[peek_idx];
				int glyph_w = static_cast<int>(utf8::display_width(rg.ch));

				if (display_width_consumed + glyph_w > available_width) {
					break;
				}

				if (rg.ch == " " || rg.ch == "\t") {
					last_space_idx = peek_idx;
					last_space_display_w = display_width_consumed;
				}

				peek_idx++;
				display_width_consumed += glyph_w;
				chunk_len++;
			}

			if (peek_idx < line_glyphs.size() && last_space_idx != std::string::npos && last_space_idx > glyph_idx) {
				chunk_len = last_space_idx - glyph_idx;
				display_width_consumed = last_space_display_w;
			}

			if (chunk_len == 0)
				break; // Safety net

			std::vector<rich_char> wrapped_glyphs;

			size_t prefix_byte_off = 0;
			std::string prefix_char;
			while (utf8::next_character(current_prefix, prefix_byte_off, prefix_char)) {
				rich_char rc;
				rc.ch = prefix_char;
				rc.color_pair = current_color;
				rc.attr = 0;
				wrapped_glyphs.push_back(rc);
			}

			for (size_t k = 0; k < chunk_len; ++k) {
				size_t original_pos = glyph_idx + k;
				rich_char rc = line_glyphs[original_pos];

				if (in_code_block && active_highlighter && !line_str.starts_with("```") && !line_attrs.empty()) {
					if (original_pos < line_attrs.size()) {
						syntax_attribute attr = line_attrs[original_pos];
						if (attr != syntax_attribute::normal) {
							rc.color_pair = syntax_color_manager::get_instance().get_color_pair(attr);
						}
					}
				}
				wrapped_glyphs.push_back(rc);
			}

			interaction_line new_line_item;
			new_line_item.set_glyphs(wrapped_glyphs);
			new_line_item.color_pair = current_color;

			bool has_custom = false;
			for (const auto &wg : wrapped_glyphs) {
				if (wg.color_pair != current_color || wg.attr != 0) {
					has_custom = true;
					break;
				}
			}
			if (!has_custom) {
				new_line_item.char_color_pairs.clear();
				new_line_item.char_attrs.clear();
			}

			lines.push_back(new_line_item);

			glyph_idx += chunk_len;

			while (glyph_idx < line_glyphs.size() && (line_glyphs[glyph_idx].ch == " " || line_glyphs[glyph_idx].ch == "\t")) {
				glyph_idx++;
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