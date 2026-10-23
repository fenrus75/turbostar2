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

void mask_utf8_char(std::string &out, size_t &idx, size_t max_end)
{
	if (idx >= max_end || idx >= out.size()) {
		return;
	}

	unsigned char lead = static_cast<unsigned char>(out[idx]);
	size_t clen = utf8::char_len(lead);

	// If sequence exceeds our masking boundary or the string length, fill single byte
	if (idx + clen > max_end || idx + clen > out.size()) {
		out[idx] = ' ';
		idx += 1;
		return;
	}

	switch (clen) {
	case 1:
		out[idx] = ' ';
		idx += 1;
		break;
	case 2:
		// U+00A0: No-Break Space (\xC2\xA0), recognized by utf8::is_space_at_byte
		out[idx] = '\xC2';
		out[idx + 1] = '\xA0';
		idx += 2;
		break;
	case 3:
		// U+2000: En Quad (\xE2\x80\x80), recognized by utf8::is_space_at_byte
		out[idx] = '\xE2';
		out[idx + 1] = '\x80';
		out[idx + 2] = '\x80';
		idx += 3;
		break;
	case 4:
		// U+F0020: Plane 15 Private Use Area whitespace (\xF3\xB0\x80\xA0)
		// Preserves exact 4 bytes and 1:1 character count invariant, recognized by utf8::is_space_at_byte
		out[idx] = '\xF3';
		out[idx + 1] = '\xB0';
		out[idx + 2] = '\x80';
		out[idx + 3] = '\xA0';
		idx += 4;
		break;
	default:
		out[idx] = ' ';
		idx += 1;
		break;
	}
}

void mask_range(std::string &out, size_t start, size_t end)
{
	size_t i = start;
	while (i < end && i < out.size()) {
		mask_utf8_char(out, i, end);
	}
}

} // anonymous namespace

std::string source_masker::mask_line(std::string_view line, mask_state &state)
{
	std::string out(line);
	const size_t len = line.size();
	size_t i = 0;

	// Detect if line is an active #include directive (header-name in quotes must be preserved)
	bool is_include_line = false;
	if (!state.in_block_comment && !state.in_raw_string) {
		size_t first_non = line.find_first_not_of(" \t");
		if (first_non != std::string_view::npos && line[first_non] == '#') {
			std::string_view rest = line.substr(first_non + 1);
			size_t inc_pos = rest.find_first_not_of(" \t");
			if (inc_pos != std::string_view::npos && rest.substr(inc_pos).starts_with("include")) {
				is_include_line = true;
			}
		}
	}

	// Resume multi-line block comment
	if (state.in_block_comment) {
		size_t close_pos = line.find("*/");
		if (close_pos != std::string_view::npos) {
			mask_range(out, 0, close_pos + 2);
			state.in_block_comment = false;
			i = close_pos + 2;
		} else {
			mask_range(out, 0, out.size());
			return out;
		}
	}

	// Resume multi-line raw string literal
	if (state.in_raw_string) {
		std::string close_pat = ")" + state.raw_string_delim + "\"";
		size_t close_pos = line.find(close_pat);
		if (close_pos != std::string_view::npos) {
			size_t close_end = close_pos + close_pat.size();
			mask_range(out, 0, close_end);
			state.in_raw_string = false;
			state.raw_string_delim.clear();
			i = close_end;
		} else {
			mask_range(out, 0, out.size());
			return out;
		}
	}

	while (i < len) {
		// 1. Line comment: //
		if (line[i] == '/' && i + 1 < len && line[i + 1] == '/') {
			mask_range(out, i, len);
			break;
		}

		// 2. Block comment: /* ... */
		if (line[i] == '/' && i + 1 < len && line[i + 1] == '*') {
			size_t close_pos = line.find("*/", i + 2);
			if (close_pos != std::string_view::npos) {
				mask_range(out, i, close_pos + 2);
				i = close_pos + 2;
			} else {
				mask_range(out, i, len);
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
							mask_range(out, prefix_start, close_end);
							i = close_end;
						} else {
							mask_range(out, prefix_start, len);
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

			++i;
			while (i < len) {
				if (line[i] == '\\' && i + 1 < len) {
					i += 2;
				} else if (line[i] == '"') {
					++i;
					break;
				} else {
					++i;
				}
			}
			if (!is_include_line) {
				mask_range(out, str_start, i);
			}
			continue;
		}

		// 5. Character literal: '...'
		if (line[i] == '\'') {
			size_t char_start = i;
			if (i > 0 && (line[i - 1] == 'u' || line[i - 1] == 'U' || line[i - 1] == 'L') &&
			    (i == 1 || !utf8::is_alnum(line[i - 2], "_"))) {
				char_start = i - 1;
			}
			++i;
			while (i < len) {
				if (line[i] == '\\' && i + 1 < len) {
					i += 2;
				} else if (line[i] == '\'') {
					++i;
					break;
				} else {
					++i;
				}
			}
			mask_range(out, char_start, i);
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
