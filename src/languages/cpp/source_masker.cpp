#include "languages/cpp/source_masker.h"
#include <algorithm>
#include "utf8.h"

namespace languages::cpp
{

namespace
{

bool is_valid_raw_string_delim(std::string_view delim)
{
	if (delim.size() > 16) {
		return false;
	}
	for (char c : delim) {
		if (c == '(' || c == ')' || c == '\\' || c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\n' ||
		    c == '\r') {
			return false;
		}
	}
	return true;
}

} // anonymous namespace

std::string source_masker::mask_line(std::string_view line, mask_state &state)
{
	std::string out(line);
	const size_t len = line.size();
	size_t i = 0;

	// Resume multi-line block comment
	if (state.in_block_comment) {
		size_t close_pos = line.find("*/");
		if (close_pos != std::string_view::npos) {
			std::fill(out.begin(), out.begin() + close_pos + 2, ' ');
			state.in_block_comment = false;
			i = close_pos + 2;
		} else {
			std::fill(out.begin(), out.end(), ' ');
			return out;
		}
	}

	// Resume multi-line raw string literal
	if (state.in_raw_string) {
		std::string close_pat = ")" + state.raw_string_delim + "\"";
		size_t close_pos = line.find(close_pat);
		if (close_pos != std::string_view::npos) {
			size_t close_end = close_pos + close_pat.size();
			std::fill(out.begin(), out.begin() + close_end, ' ');
			state.in_raw_string = false;
			state.raw_string_delim.clear();
			i = close_end;
		} else {
			std::fill(out.begin(), out.end(), ' ');
			return out;
		}
	}

	while (i < len) {
		// 1. Line comment: //
		if (line[i] == '/' && i + 1 < len && line[i + 1] == '/') {
			std::fill(out.begin() + i, out.end(), ' ');
			break;
		}

		// 2. Block comment: /* ... */
		if (line[i] == '/' && i + 1 < len && line[i + 1] == '*') {
			size_t close_pos = line.find("*/", i + 2);
			if (close_pos != std::string_view::npos) {
				std::fill(out.begin() + i, out.begin() + close_pos + 2, ' ');
				i = close_pos + 2;
			} else {
				std::fill(out.begin() + i, out.end(), ' ');
				state.in_block_comment = true;
				break;
			}
			continue;
		}

		// 3. Raw string literal: R"delim(...)delim" (including prefixes like u8R", uR", UR", LR")
		if (line[i] == 'R' && i + 1 < len && line[i + 1] == '"') {
			bool valid_prefix = (i == 0 || !utf8::is_alnum(line[i - 1], "_"));
			size_t prefix_start = i;
			if (i > 0 && line[i - 1] == '8' && i > 1 && line[i - 2] == 'u' &&
			    (i == 2 || !utf8::is_alnum(line[i - 3], "_"))) {
				valid_prefix = true;
				prefix_start = i - 2;
			} else if (i > 0 && (line[i - 1] == 'u' || line[i - 1] == 'U' || line[i - 1] == 'L') &&
				   (i == 1 || !utf8::is_alnum(line[i - 2], "_"))) {
				valid_prefix = true;
				prefix_start = i - 1;
			}

			if (valid_prefix) {
				size_t open_paren = line.find('(', i + 2);
				if (open_paren != std::string_view::npos) {
					std::string_view delim = line.substr(i + 2, open_paren - (i + 2));
					if (is_valid_raw_string_delim(delim)) {
						std::string close_pat = ")" + std::string(delim) + "\"";
						size_t close_pos = line.find(close_pat, open_paren + 1);
						if (close_pos != std::string_view::npos) {
							size_t close_end = close_pos + close_pat.size();
							std::fill(out.begin() + prefix_start, out.begin() + close_end, ' ');
							i = close_end;
						} else {
							std::fill(out.begin() + prefix_start, out.end(), ' ');
							state.in_raw_string = true;
							state.raw_string_delim = std::string(delim);
							break;
						}
						continue;
					}
				}
			}
		}

		// 4. Standard string literal: "..."
		if (line[i] == '"') {
			size_t str_start = i;
			// Also check for prefixes L", u8", u", U"
			if (i > 0 && line[i - 1] == '8' && i > 1 && line[i - 2] == 'u' &&
			    (i == 2 || !utf8::is_alnum(line[i - 3], "_"))) {
				str_start = i - 2;
			} else if (i > 0 && (line[i - 1] == 'u' || line[i - 1] == 'U' || line[i - 1] == 'L') &&
				   (i == 1 || !utf8::is_alnum(line[i - 2], "_"))) {
				str_start = i - 1;
			}

			out[i] = ' ';
			++i;
			while (i < len) {
				if (line[i] == '\\' && i + 1 < len) {
					out[i] = ' ';
					out[i + 1] = ' ';
					i += 2;
				} else if (line[i] == '"') {
					out[i] = ' ';
					++i;
					break;
				} else {
					out[i] = ' ';
					++i;
				}
			}
			std::fill(out.begin() + str_start, out.begin() + (str_start < i ? str_start : i), ' ');
			continue;
		}

		// 5. Character literal: '...'
		if (line[i] == '\'') {
			size_t char_start = i;
			if (i > 0 && (line[i - 1] == 'u' || line[i - 1] == 'U' || line[i - 1] == 'L') &&
			    (i == 1 || !utf8::is_alnum(line[i - 2], "_"))) {
				char_start = i - 1;
			}
			out[i] = ' ';
			++i;
			while (i < len) {
				if (line[i] == '\\' && i + 1 < len) {
					out[i] = ' ';
					out[i + 1] = ' ';
					i += 2;
				} else if (line[i] == '\'') {
					out[i] = ' ';
					++i;
					break;
				} else {
					out[i] = ' ';
					++i;
				}
			}
			std::fill(out.begin() + char_start, out.begin() + (char_start < i ? char_start : i), ' ');
			continue;
		}

		++i;
	}

	return out;
}

std::vector<std::string> source_masker::mask_lines(std::span<const std::string> lines)
{
	std::vector<std::string> result;
	result.reserve(lines.size());
	mask_state state;
	for (const auto &line : lines) {
		result.push_back(mask_line(line, state));
	}
	return result;
}

} // namespace languages::cpp
