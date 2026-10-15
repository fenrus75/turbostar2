#include "utf8.h"
#include <atomic>
#include <cctype>
#include <wchar.h>
#include "mime.h"

namespace utf8
{

size_t length(std::string_view s) noexcept
{
	size_t offset = 0;
	size_t chars = 0;
	while (offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		size_t clen = char_len(c);
		if (offset + clen > s.length()) {
			clen = s.length() - offset;
		}
		offset += clen;
		chars++;
	}
	return chars;
}

size_t display_width(std::string_view s) noexcept
{
	size_t total_width = 0;
	size_t offset = 0;
	mbstate_t state = {};
	while (offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		if (c < 0x80) {
			total_width += 1;
			offset += 1;
			continue;
		}
		wchar_t wc = 0;
		size_t res = mbrtowc(&wc, s.data() + offset, s.length() - offset, &state);
		if (res == static_cast<size_t>(-1) || res == static_cast<size_t>(-2)) {
			// Invalid or incomplete char, assume width 1 and advance 1 byte
			total_width += 1;
			offset += 1;
		} else if (res == 0) {
			// NUL byte, width 0
			offset += 1;
		} else {
			int w = wcwidth(wc);
			if (w >= 0) {
				total_width += static_cast<size_t>(w);
			}
			offset += res;
		}
	}
	return total_width;
}

size_t char_to_byte_offset(std::string_view s, size_t char_pos) noexcept
{
	size_t offset = 0;
	size_t chars = 0;
	while (chars < char_pos && offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		size_t clen = char_len(c);
		if (offset + clen > s.length()) {
			clen = s.length() - offset;
		}
		offset += clen;
		chars++;
	}
	return offset;
}

size_t byte_to_char_pos(std::string_view s, size_t byte_offset) noexcept
{
	size_t offset = 0;
	size_t chars = 0;
	while (offset < byte_offset && offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		size_t clen = char_len(c);
		if (offset + clen > s.length()) {
			clen = s.length() - offset;
		}
		offset += clen;
		chars++;
	}
	return chars;
}

bool next_character(std::string_view s, size_t &byte_offset, std::string &out_char)
{
	if (byte_offset >= s.length()) {
		out_char.clear();
		return false;
	}

	unsigned char c = static_cast<unsigned char>(s[byte_offset]);
	if (c < 0x80) {
		out_char.assign(1, static_cast<char>(c));
		byte_offset++;
		return true;
	}

	size_t clen = char_len(c);
	if (byte_offset + clen > s.length()) {
		clen = s.length() - byte_offset;
	}

	out_char.assign(s, byte_offset, clen);
	byte_offset += clen;
	return true;
}

std::string sanitize(std::string_view s)
{
	std::string res;
	res.reserve(s.length());
	size_t offset = 0;
	while (offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		if (c < 0x80) {
			res.push_back(s[offset]);
			offset++;
			continue;
		}
		size_t clen = char_len(c);
		if (clen == 1) {
			// Invalid UTF-8 lead byte (continuation byte or invalid value)
			res.push_back('?');
			offset++;
			continue;
		}
		if (offset + clen > s.length()) {
			res.append(s.length() - offset, '?');
			break;
		}
		// Verify continuation bytes
		bool valid = true;
		for (size_t i = 1; i < clen; ++i) {
			unsigned char next_c = static_cast<unsigned char>(s[offset + i]);
			if ((next_c & 0xC0) != 0x80) {
				valid = false;
				break;
			}
		}
		if (valid) {
			res.append(s.data() + offset, clen);
			offset += clen;
		} else {
			res.push_back('?');
			offset++; // consume lead byte
		}
	}
	return res;
}

bool is_valid_utf8(std::string_view s) noexcept
{
	size_t i = 0;
	while (i < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c == 0) {
			return false; // Null bytes are not treated as plain text
		}
		if (c < 0x80) {
			i++;
			continue;
		}
		size_t len = char_len(c);
		if (len == 1) {
			return false; // Invalid lead byte
		}
		if (i + len > s.length()) {
			return false; // Truncated UTF-8 character
		}
		for (size_t j = 1; j < len; ++j) {
			unsigned char next_c = static_cast<unsigned char>(s[i + j]);
			if ((next_c & 0xC0) != 0x80) {
				return false; // Invalid continuation byte
			}
		}
		i += len;
	}
	return true;
}

std::string trim(std::string_view s)
{
	auto first = s.find_first_not_of(" \t\r\n");
	if (std::string_view::npos == first)
		return "";
	auto last = s.find_last_not_of(" \t\r\n");
	return std::string(s.substr(first, (last - first + 1)));
}

void trim_trailing_whitespace(std::string &s)
{
	auto last = s.find_last_not_of(" \t\r\n");
	if (std::string::npos == last) {
		s.clear();
	} else {
		s.erase(last + 1);
	}
}

std::vector<std::string> wrap_string(std::string_view prefix, std::string_view text, int width)
{
	std::vector<std::string> lines;
	int prefix_utf8_len = static_cast<int>(utf8::display_width(prefix));

	if (width <= prefix_utf8_len + 4) {
		lines.push_back(std::string(prefix) + std::string(text));
		return lines;
	}

	int available_width = width - prefix_utf8_len;
	std::string current_prefix(prefix);
	size_t byte_idx = 0;

	while (byte_idx < text.length()) {
		size_t chunk_byte_len = 0;
		int chars_consumed = 0;
		size_t last_space_byte_idx = std::string::npos;
		size_t last_space_chars = 0;

		size_t peek_idx = byte_idx;
		while (peek_idx < text.length()) {
			unsigned char c = static_cast<unsigned char>(text[peek_idx]);
			size_t char_bytes = utf8::char_len(c);

			if (peek_idx + char_bytes > text.length()) {
				char_bytes = text.length() - peek_idx;
			}

			std::string_view glyph(text.data() + peek_idx, char_bytes);
			size_t glyph_w = utf8::display_width(glyph);

			if (chars_consumed + static_cast<int>(glyph_w) > available_width) {
				break;
			}

			if (c == ' ' || c == '\t') {
				last_space_byte_idx = peek_idx;
				last_space_chars = chars_consumed;
			}

			peek_idx += char_bytes;
			chars_consumed += glyph_w;
			chunk_byte_len += char_bytes;
		}

		if (peek_idx < text.length() && text[peek_idx] != ' ' && text[peek_idx] != '\t' &&
		    last_space_byte_idx != std::string::npos && last_space_byte_idx > byte_idx) {
			chunk_byte_len = last_space_byte_idx - byte_idx;
			chars_consumed = last_space_chars;
		}

		if (chunk_byte_len == 0) {
			unsigned char c = static_cast<unsigned char>(text[byte_idx]);
			size_t char_bytes = utf8::char_len(c);
			if (byte_idx + char_bytes > text.length()) {
				char_bytes = text.length() - byte_idx;
			}
			chunk_byte_len = char_bytes;
		}

		lines.push_back(current_prefix + std::string(text.substr(byte_idx, chunk_byte_len)));
		byte_idx += chunk_byte_len;

		while (byte_idx < text.length() && (text[byte_idx] == ' ' || text[byte_idx] == '\t')) {
			byte_idx++;
		}

		current_prefix = std::string(prefix_utf8_len, ' ');
	}

	if (lines.empty()) {
		lines.push_back(std::string(prefix));
	}

	return lines;
}

std::string detect_mime(std::string_view buffer)
{
	return mime::detect_buffer_type(buffer);
}

std::string sanitize_terminal_output(std::string_view input)
{
	std::string output;
	output.reserve(input.length());
	size_t i = 0;
	while (i < input.length()) {
		bool is_esc = false;
		size_t esc_len = 0;
		if (input[i] == '\x1b') {
			is_esc = true;
			esc_len = 1;
		} else if (i + 3 < input.length() && input[i] == '\\' && input[i + 1] == 'x' && input[i + 2] == '1' && (input[i + 3] == 'b' || input[i + 3] == 'B')) {
			is_esc = true;
			esc_len = 4;
		} else if (i + 5 < input.length() && input[i] == '\\' && input[i + 1] == 'u' && input[i + 2] == '0' && input[i + 3] == '0' && input[i + 4] == '1' && (input[i + 5] == 'b' || input[i + 5] == 'B')) {
			is_esc = true;
			esc_len = 6;
		}

		if (is_esc) {
			if (i + esc_len < input.length() && input[i + esc_len] == '[') {
				// CSI sequence: ESC [ ... [command character]
				size_t j = i + esc_len + 1;
				while (j < input.length()) {
					char c = input[j];
					if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '~' || c == '@') {
						i = j + 1;
						break;
					}
					j++;
				}
				if (j == input.length()) {
					// Incomplete CSI sequence reached end of input: discard trailing escape fragment
					break;
				}
			} else if (i + esc_len < input.length() && input[i + esc_len] == ']') {
				// OSC sequence: ESC ] ... (terminated by BEL \x07 or ST ESC \)
				size_t j = i + esc_len + 1;
				while (j < input.length()) {
					if (input[j] == '\x07') {
						i = j + 1;
						break;
					}
					if (input[j] == '\x1b' && j + 1 < input.length() && input[j + 1] == '\\') {
						i = j + 2;
						break;
					}
					j++;
				}
				if (j == input.length()) {
					// Incomplete OSC sequence reached end of input: discard trailing escape fragment
					break;
				}
			} else if (i + esc_len >= input.length()) {
				// Trailing bare ESC byte at the end of input: discard it
				break;
			} else {
				// 2-byte escape sequence like ESC 7, ESC 8, ESC =, ESC >
				i += esc_len + 1;
			}
		} else {
			output += input[i];
			i++;
		}
	}
	return output;
}

// Converts an identifier from PascalCase, camelCase, kebab-case, or SCREAMING_SNAKE_CASE
// into canonical snake_case. Preserves acronym cohesion (e.g., "HTMLParser" -> "html_parser",
// "URL" -> "url") and separates digit-to-uppercase boundaries (e.g., "utf8Char" -> "utf8_char").
std::string to_snake_case(std::string_view s)
{
	std::string res;
	res.reserve(s.size() + 4);

	for (size_t i = 0; i < s.size(); ++i) {
		unsigned char uc = static_cast<unsigned char>(s[i]);
		if (uc == '_' || uc == '-' || uc == ' ') {
			if (!res.empty() && res.back() != '_') {
				res.push_back('_');
			}
			continue;
		}

		if (std::isupper(uc)) {
			if (!res.empty() && res.back() != '_') {
				unsigned char prev = static_cast<unsigned char>(s[i - 1]);
				bool prev_is_lower_or_digit = std::islower(prev) || std::isdigit(prev) || (prev >= 0x80);
				bool prev_is_upper = std::isupper(prev) != 0;
				bool next_is_lower =
				    (i + 1 < s.size()) && (std::islower(static_cast<unsigned char>(s[i + 1])) != 0);
				if (prev_is_lower_or_digit || (prev_is_upper && next_is_lower)) {
					res.push_back('_');
				}
			}
			res.push_back(static_cast<char>(std::tolower(uc)));
		} else {
			res.push_back(s[i]);
		}
	}

	while (!res.empty() && res.back() == '_') {
		res.pop_back();
	}
	return res;
}

// Converts an identifier to PascalCase (Google/Antigravity parameter style) by first
// normalizing word boundaries via to_snake_case and capitalizing each segment's initial letter.
std::string to_PascalCase(std::string_view s)
{
	std::string snake = to_snake_case(s);
	std::string res;
	res.reserve(snake.size());

	bool capitalize_next = true;
	for (char c : snake) {
		if (c == '_') {
			capitalize_next = true;
			continue;
		}
		unsigned char uc = static_cast<unsigned char>(c);
		if (capitalize_next && std::islower(uc)) {
			res.push_back(static_cast<char>(std::toupper(uc)));
		} else {
			if (capitalize_next && !res.empty() && res.back() != '_') {
				res.push_back('_');
			}
			res.push_back(c);
		}
		capitalize_next = false;
	}
	return res;
}

// Converts an identifier to camelCase (Microsoft/Copilot parameter style) by deriving
// PascalCase and lowercasing the leading ASCII character.
std::string to_camelCase(std::string_view s)
{
	std::string pascal = to_PascalCase(s);
	if (!pascal.empty()) {
		unsigned char first = static_cast<unsigned char>(pascal[0]);
		if (std::isupper(first)) {
			pascal[0] = static_cast<char>(std::tolower(first));
		}
	}
	return pascal;
}

static std::atomic<param_case_style> g_active_param_case_style{param_case_style::snake_case};

void set_active_param_case_style(param_case_style style) noexcept
{
	g_active_param_case_style.store(style, std::memory_order_relaxed);
}

param_case_style get_active_param_case_style() noexcept
{
	return g_active_param_case_style.load(std::memory_order_relaxed);
}

std::string format_param_name(std::string_view snake_param)
{
	switch (get_active_param_case_style()) {
	case param_case_style::pascal_case:
		return to_PascalCase(snake_param);
	case param_case_style::camel_case:
		return to_camelCase(snake_param);
	case param_case_style::snake_case:
	default:
		return std::string(snake_param);
	}
}

std::string format_markdown_table_header(std::string_view header_line)
{
	if (get_active_param_case_style() == param_case_style::snake_case) {
		return std::string(header_line);
	}

	std::string res;
	res.reserve(header_line.size() + 8);

	size_t i = 0;
	while (i < header_line.size()) {
		unsigned char uc = static_cast<unsigned char>(header_line[i]);
		if (std::islower(uc)) {
			size_t start = i;
			while (i < header_line.size()) {
				unsigned char cur = static_cast<unsigned char>(header_line[i]);
				if (!std::isalnum(cur) && cur != '_') {
					break;
				}
				++i;
			}
			res.append(format_param_name(header_line.substr(start, i - start)));
		} else if (std::isalnum(uc) || uc == '_') {
			while (i < header_line.size()) {
				unsigned char cur = static_cast<unsigned char>(header_line[i]);
				if (!std::isalnum(cur) && cur != '_') {
					break;
				}
				res.push_back(header_line[i]);
				++i;
			}
		} else {
			res.push_back(header_line[i]);
			++i;
		}
	}

	return res;
}

} // namespace utf8
