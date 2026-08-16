#include "markdown_utils.h"
#include <algorithm>
#include <re2/re2.h>
#include <sstream>
#include "utf8.h"

namespace markdown_utils
{

bool table_aligner::is_table_row(const std::string &line)
{
	static const re2::RE2 row_re("(?:\\|.*[^|\\s]|[^|\\s].*\\|)");
	return re2::RE2::PartialMatch(line, row_re);
}

bool table_aligner::is_header_separator(const std::string &line)
{
	static const re2::RE2 sep_re("^(?:[-:\\s]*-[-|:\\s]*\\||[|:\\s]*\\|[-|:\\s]*-)[-|:\\s]*$");
	return re2::RE2::FullMatch(line, sep_re);
}

std::vector<table_range> table_aligner::find_table_ranges(const std::vector<std::string> &lines)
{
	std::vector<table_range> ranges;

	size_t i = 0;
	while (i < lines.size()) {
		if (is_table_row(lines[i])) {
			// Potential table start. A valid table must have a header separator.
			size_t potential_start = i;
			bool found_separator = false;
			size_t end_line = i;

			size_t j = i;
			while (j < lines.size() && is_table_row(lines[j])) {
				if (is_header_separator(lines[j])) {
					found_separator = true;
				}
				end_line = j;
				j++;
			}

			if (found_separator) {
				ranges.push_back({potential_start, end_line});
				i = end_line + 1;
			} else {
				i++;
			}
		} else {
			i++;
		}
	}

	return ranges;
}

std::vector<std::string> table_aligner::tokenize_row(const std::string &line)
{
	std::vector<std::string> tokens;
	std::string current;
	bool escaped = false;

	// Handle rows that start and end with pipes
	std::string trimmed = trim(line);
	size_t start = 0;
	size_t end = trimmed.length();
	if (!trimmed.empty() && trimmed.front() == '|')
		start = 1;
	if (trimmed.length() > 1 && trimmed.back() == '|')
		end = trimmed.length() - 1;

	for (size_t i = start; i < end; ++i) {
		char c = trimmed[i];
		if (escaped) {
			current += c;
			escaped = false;
		} else if (c == '\\') {
			escaped = true;
		} else if (c == '|') {
			tokens.push_back(trim(current));
			current.clear();
		} else {
			current += c;
		}
	}
	tokens.push_back(trim(current));
	return tokens;
}

std::string table_aligner::trim(const std::string &s)
{
	return utf8::trim(s);
}

size_t display_width(const std::string &s)
{
	return utf8::display_width(s);
}

static size_t formatted_display_width(const std::string &s)
{
	size_t total_width = 0;
	bool is_inline_code = false;

	size_t i = 0;
	while (i < s.length()) {
		if (!is_inline_code && i + 1 < s.length() && s.substr(i, 2) == "**") {
			i += 2;
			continue;
		}
		if (s[i] == '`') {
			is_inline_code = !is_inline_code;
			i += 1;
			continue;
		}

		unsigned char c = static_cast<unsigned char>(s[i]);
		size_t clen = utf8::char_len(c);
		if (i + clen > s.length()) {
			clen = s.length() - i;
		}
		std::string_view glyph(s.data() + i, clen);
		total_width += utf8::display_width(glyph);
		i += clen;
	}
	return total_width;
}

static std::string truncate_cell(const std::string &content, size_t target_width)
{
	size_t disp_w = formatted_display_width(content);
	if (disp_w <= target_width) {
		return content;
	}

	if (target_width <= 3) {
		size_t byte_offset = 0;
		std::string result;
		std::string glyph;
		size_t current_w = 0;
		while (utf8::next_character(content, byte_offset, glyph)) {
			size_t glyph_w = utf8::display_width(glyph);
			if (current_w + glyph_w > target_width) {
				break;
			}
			result += glyph;
			current_w += glyph_w;
		}
		return result;
	} else {
		size_t limit = target_width - 3;
		size_t byte_offset = 0;
		std::string result;
		std::string glyph;
		size_t current_w = 0;
		while (utf8::next_character(content, byte_offset, glyph)) {
			size_t glyph_w = utf8::display_width(glyph);
			if (current_w + glyph_w > limit) {
				break;
			}
			result += glyph;
			current_w += glyph_w;
		}
		result += "...";
		return result;
	}
}

static std::vector<std::string> wrap_cell(const std::string &content, size_t target_width)
{
	if (content.empty()) {
		return {""};
	}
	if (target_width == 0) {
		return {content};
	}
	if (formatted_display_width(content) <= target_width) {
		return {content};
	}

	std::vector<std::string> lines;
	size_t byte_idx = 0;
	size_t text_len = content.length();

	while (byte_idx < text_len) {
		size_t line_start = byte_idx;
		size_t current_byte_len = 0;
		size_t current_disp_width = 0;
		size_t last_space_byte_idx = std::string::npos;

		bool is_inline_code = false;
		size_t peek_idx = byte_idx;

		while (peek_idx < text_len) {
			if (!is_inline_code && peek_idx + 1 < text_len && content.substr(peek_idx, 2) == "**") {
				peek_idx += 2;
				current_byte_len += 2;
				continue;
			}
			if (content[peek_idx] == '`') {
				is_inline_code = !is_inline_code;
				peek_idx += 1;
				current_byte_len += 1;
				continue;
			}

			unsigned char c = static_cast<unsigned char>(content[peek_idx]);
			size_t clen = utf8::char_len(c);
			if (peek_idx + clen > text_len) {
				clen = text_len - peek_idx;
			}
			std::string_view glyph(content.data() + peek_idx, clen);
			size_t glyph_w = utf8::display_width(glyph);

			if (current_disp_width + glyph_w > target_width && current_byte_len > 0) {
				break;
			}

			if (c == ' ' || c == '\t') {
				last_space_byte_idx = peek_idx;
			}

			peek_idx += clen;
			current_disp_width += glyph_w;
			current_byte_len += clen;
		}

		if (peek_idx < text_len && content[peek_idx] != ' ' && content[peek_idx] != '\t' &&
		    last_space_byte_idx != std::string::npos && last_space_byte_idx > line_start) {
			current_byte_len = last_space_byte_idx - line_start;
		}

		if (current_byte_len == 0) {
			unsigned char c = static_cast<unsigned char>(content[byte_idx]);
			size_t clen = utf8::char_len(c);
			if (byte_idx + clen > text_len) {
				clen = text_len - byte_idx;
			}
			current_byte_len = clen;
		}

		lines.push_back(content.substr(line_start, current_byte_len));
		byte_idx = line_start + current_byte_len;

		while (byte_idx < text_len && (content[byte_idx] == ' ' || content[byte_idx] == '\t')) {
			byte_idx++;
		}
	}

	if (lines.empty()) {
		lines.push_back("");
	}
	return lines;
}

std::string align_all_tables(const std::string &text, bool framed, int min_width, int max_width, bool word_wrap)
{
	std::vector<std::string> lines;
	std::stringstream ss(text);
	std::string line;
	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
	}

	if (!text.empty() && text.back() == '\n') {
		lines.push_back("");
	}

	auto ranges = table_aligner::find_table_ranges(lines);
	if (ranges.empty())
		return text;

	std::vector<std::string> processed_lines = lines;
	for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
		std::vector<std::string> table_block;
		for (size_t i = it->start_line; i <= it->end_line; ++i) {
			table_block.push_back(processed_lines[i]);
		}

		align_options opts;
		opts.use_utf8_frames = framed;
		opts.word_wrap = word_wrap;
		auto aligned = table_aligner::align_table_block(table_block, opts, min_width, max_width);

		processed_lines.erase(processed_lines.begin() + it->start_line, processed_lines.begin() + it->end_line + 1);
		processed_lines.insert(processed_lines.begin() + it->start_line, aligned.begin(), aligned.end());
	}

	std::string result;
	result.reserve(text.size() + processed_lines.size());
	for (size_t i = 0; i < processed_lines.size(); ++i) {
		result += processed_lines[i];
		if (i < processed_lines.size() - 1)
			result += "\n";
	}
	return result;
}

std::vector<std::string> table_aligner::align_table_block(const std::vector<std::string> &lines, const align_options &opts, int min_width, int max_width)
{
	if (lines.empty())
		return {};

	std::vector<std::vector<std::string>> grid;
	std::vector<size_t> col_widths;

	for (const auto &line : lines) {
		if (is_header_separator(line)) {
			grid.push_back({"---SEPARATOR---"}); // Placeholder
			continue;
		}

		auto tokens = tokenize_row(line);
		grid.push_back(tokens);

		if (tokens.size() > col_widths.size()) {
			col_widths.resize(tokens.size(), 0);
		}

		for (size_t i = 0; i < tokens.size(); ++i) {
			col_widths[i] = std::max(col_widths[i], formatted_display_width(tokens[i]));
		}
	}

	// Adjust column widths based on min_width and max_width
	if (!col_widths.empty()) {
		size_t N = col_widths.size();
		size_t framing_chars = (opts.use_outer_pipes || opts.use_utf8_frames) ? 2 : 0;
		size_t separators_chars = N - 1;
		size_t padding_chars = N * 2 * opts.padding;
		size_t content_chars = 0;
		for (size_t w : col_widths) {
			content_chars += w;
		}
		size_t current_width = framing_chars + separators_chars + padding_chars + content_chars;

		// Case A: Natural width exceeds max_width -> Shrink
		if (max_width > 0 && current_width > static_cast<size_t>(max_width)) {
			size_t target_content_chars = static_cast<size_t>(max_width);
			size_t overhead = framing_chars + separators_chars + padding_chars;
			if (target_content_chars > overhead) {
				target_content_chars -= overhead;
			} else {
				target_content_chars = 0;
			}

			while (true) {
				size_t total_content = 0;
				for (size_t w : col_widths) {
					total_content += w;
				}
				if (total_content <= target_content_chars) {
					break;
				}

				size_t widest_idx = 0;
				size_t max_w = col_widths[0];
				for (size_t i = 1; i < col_widths.size(); ++i) {
					if (col_widths[i] > max_w) {
						max_w = col_widths[i];
						widest_idx = i;
					}
				}

				if (max_w == 0) {
					break;
				}
				col_widths[widest_idx]--;
			}
		}
		// Case B: Natural width is below min_width -> Expand
		else if (min_width > 0 && current_width < static_cast<size_t>(min_width)) {
			size_t needed = static_cast<size_t>(min_width) - current_width;
			size_t extra_per_col = (needed + N - 1) / N; // Round up to add space evenly
			std::vector<size_t> col_add(N, extra_per_col);
			size_t new_width = current_width + N * extra_per_col;

			if (max_width > 0 && new_width > static_cast<size_t>(max_width)) {
				size_t overshoot = new_width - static_cast<size_t>(max_width);
				for (int i = static_cast<int>(N) - 1; i >= 0 && overshoot > 0; --i) {
					if (col_add[i] > 0) {
						col_add[i]--;
						overshoot--;
					}
				}
			}

			for (size_t i = 0; i < N; ++i) {
				col_widths[i] += col_add[i];
			}
		}
	}

	std::vector<std::string> result;

	auto make_border = [&](const std::string &left, const std::string &mid, const std::string &right, const std::string &fill) {
		std::string line = left;
		for (size_t i = 0; i < col_widths.size(); ++i) {
			if (i > 0)
				line += mid;
			for (size_t j = 0; j < col_widths[i] + 2 * opts.padding; ++j)
				line += fill;
		}
		line += right;
		return line;
	};

	if (opts.use_utf8_frames) {
		result.push_back(make_border("┌", "┬", "┐", "─"));
	}

	for (size_t row_idx = 0; row_idx < grid.size(); ++row_idx) {
		const auto &row = grid[row_idx];

		if (row.size() == 1 && row[0] == "---SEPARATOR---") {
			if (opts.use_utf8_frames) {
				result.push_back(make_border("├", "┼", "┤", "─"));
			} else {
				std::string aligned_line;
				if (opts.use_outer_pipes)
					aligned_line += "|";
				for (size_t i = 0; i < col_widths.size(); ++i) {
					if (i > 0)
						aligned_line += "|";
					aligned_line += std::string(col_widths[i] + 2 * opts.padding, '-');
				}
				if (opts.use_outer_pipes)
					aligned_line += "|";
				result.push_back(aligned_line);
			}
			continue;
		}

		if (opts.word_wrap) {
			std::vector<std::vector<std::string>> cell_sub_lines(col_widths.size());
			size_t max_lines = 1;

			for (size_t i = 0; i < col_widths.size(); ++i) {
				std::string cell = (i < row.size()) ? row[i] : "";
				cell_sub_lines[i] = wrap_cell(cell, col_widths[i]);
				max_lines = std::max(max_lines, cell_sub_lines[i].size());
			}

			for (size_t sub_idx = 0; sub_idx < max_lines; ++sub_idx) {
				std::string aligned_line;
				if (opts.use_utf8_frames) {
					aligned_line = "│";
					for (size_t i = 0; i < col_widths.size(); ++i) {
						if (i > 0)
							aligned_line += "│";
						aligned_line += std::string(opts.padding, ' ');

						std::string sub_cell = (sub_idx < cell_sub_lines[i].size()) ? cell_sub_lines[i][sub_idx] : "";
						aligned_line += sub_cell;

						size_t cell_len = formatted_display_width(sub_cell);
						if (col_widths[i] > cell_len) {
							aligned_line += std::string(col_widths[i] - cell_len, ' ');
						}
						aligned_line += std::string(opts.padding, ' ');
					}
					aligned_line += "│";
				} else {
					if (opts.use_outer_pipes)
						aligned_line += "|";
					for (size_t i = 0; i < col_widths.size(); ++i) {
						if (i > 0)
							aligned_line += "|";
						aligned_line += std::string(opts.padding, ' ');

						std::string sub_cell = (sub_idx < cell_sub_lines[i].size()) ? cell_sub_lines[i][sub_idx] : "";
						aligned_line += sub_cell;

						size_t cell_len = formatted_display_width(sub_cell);
						if (col_widths[i] > cell_len) {
							aligned_line += std::string(col_widths[i] - cell_len, ' ');
						}

						aligned_line += std::string(opts.padding, ' ');
					}
					if (opts.use_outer_pipes)
						aligned_line += "|";
				}
				result.push_back(aligned_line);
			}
		} else {
			std::string aligned_line;
			if (opts.use_utf8_frames) {
				aligned_line = "│";
				for (size_t i = 0; i < col_widths.size(); ++i) {
					if (i > 0)
						aligned_line += "│";
					aligned_line += std::string(opts.padding, ' ');
					std::string cell = (i < row.size()) ? row[i] : "";
					cell = truncate_cell(cell, col_widths[i]);
					aligned_line += cell;
					size_t cell_len = formatted_display_width(cell);
					if (col_widths[i] > cell_len) {
						aligned_line += std::string(col_widths[i] - cell_len, ' ');
					}
					aligned_line += std::string(opts.padding, ' ');
				}
				aligned_line += "│";
			} else {
				if (opts.use_outer_pipes)
					aligned_line += "|";
				for (size_t i = 0; i < col_widths.size(); ++i) {
					if (i > 0)
						aligned_line += "|";
					aligned_line += std::string(opts.padding, ' ');

					std::string cell = (i < row.size()) ? row[i] : "";
					cell = truncate_cell(cell, col_widths[i]);
					aligned_line += cell;

					size_t cell_len = formatted_display_width(cell);
					if (col_widths[i] > cell_len) {
						aligned_line += std::string(col_widths[i] - cell_len, ' ');
					}

					aligned_line += std::string(opts.padding, ' ');
				}
				if (opts.use_outer_pipes)
					aligned_line += "|";
			}
			result.push_back(aligned_line);
		}
	}

	if (opts.use_utf8_frames) {
		result.push_back(make_border("└", "┴", "┘", "─"));
	}

	return result;
}


} // namespace markdown_utils
