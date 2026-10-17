#include "utf8.h"
#include <atomic>
#include <cctype>
#include <wchar.h>
#include "mime.h"

namespace utf8
{

// Computes the number of UTF-8 code points in string s.
// Multi-byte sequences (1 to 4 bytes) are counted as a single character.
// If an incomplete or truncated UTF-8 sequence occurs at the end of the string,
// it is clamped to the available bytes and counted as one character without reading out of bounds.
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

// Measures the visual terminal display column width of string s.
// Standard ASCII characters (< 0x80) have width 1 (fast-path).
// Multi-byte sequences are decoded via mbrtowc and measured with wcwidth():
// - East Asian Fullwidth / CJK glyphs and emojis contribute width 2.
// - ASCII bytes (< 0x80) contribute width 1 via the fast-path.
// - Invalid or incomplete byte sequences fall back to width 1 and advance 1 byte.
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

// Translates a 0-indexed character position into a byte offset within string s.
// Returns s.length() if char_pos exceeds the total number of characters.
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

// Translates a byte offset into a 0-indexed character index within string s.
// If byte_offset points into the middle of a multi-byte sequence, the returned
// character count covers the complete character containing or ending at that offset.
// Returns length(s) if byte_offset >= s.length().
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

// Extracts the next UTF-8 character string starting at byte_offset and advances byte_offset.
// Returns false and clears out_char if byte_offset is at or past the end of string s.
// Truncated trailing sequences are safely clamped to the remainder of s.
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

// Returns true if the byte at byte_offset starts an ASCII whitespace or Unicode space character.
// Returns false if byte_offset is out of range, points to a non-space, or points to a continuation byte.
bool is_space_at_byte(std::string_view s, size_t byte_offset) noexcept
{
	if (byte_offset >= s.length()) {
		return false;
	}

	unsigned char c = static_cast<unsigned char>(s[byte_offset]);
	// Fast-path: single-byte ASCII
	if (c < 0x80) {
		return std::isspace(c) != 0;
	}

	// Multibyte Unicode space sequences
	// U+00A0: No-Break Space (\xC2\xA0)
	if (c == 0xC2 && byte_offset + 1 < s.length()) {
		return static_cast<unsigned char>(s[byte_offset + 1]) == 0xA0;
	}

	// 3-byte spaces starting with 0xE1:
	// U+1680: Ogham Space Mark (\xE1\x9A\x80)
	if (c == 0xE1 && byte_offset + 2 < s.length()) {
		unsigned char b1 = static_cast<unsigned char>(s[byte_offset + 1]);
		unsigned char b2 = static_cast<unsigned char>(s[byte_offset + 2]);
		return b1 == 0x9A && b2 == 0x80;
	}

	// 3-byte spaces starting with 0xE2:
	// U+2000..U+200A (En/Em/Thin/Hair/etc. spaces), U+2028, U+2029, U+202F, U+205F
	if (c == 0xE2 && byte_offset + 2 < s.length()) {
		unsigned char b1 = static_cast<unsigned char>(s[byte_offset + 1]);
		unsigned char b2 = static_cast<unsigned char>(s[byte_offset + 2]);
		if (b1 == 0x80) {
			return (b2 >= 0x80 && b2 <= 0x8A) || b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF;
		}
		if (b1 == 0x81 && b2 == 0x9F) {
			return true;
		}
		return false;
	}

	// 3-byte spaces starting with 0xE3:
	// U+3000: Ideographic Fullwidth Space (\xE3\x80\x80)
	if (c == 0xE3 && byte_offset + 2 < s.length()) {
		unsigned char b1 = static_cast<unsigned char>(s[byte_offset + 1]);
		unsigned char b2 = static_cast<unsigned char>(s[byte_offset + 2]);
		return b1 == 0x80 && b2 == 0x80;
	}

	return false;
}

// Returns true if the UTF-8 character at char_pos is an ASCII whitespace or Unicode space character.
// Returns false if char_pos is out of range or not a space.
bool is_space_at_char(std::string_view s, size_t char_pos) noexcept
{
	size_t byte_offset = char_to_byte_offset(s, char_pos);
	return is_space_at_byte(s, byte_offset);
}

// Returns true if the character at byte_offset is alphanumeric ([0-9a-zA-Z]) or matches any character in extra.
// If the character at byte_offset is a multi-byte UTF-8 sequence, it checks if the full multi-byte character
// is contained in extra. Returns false if byte_offset is out of range or on a continuation byte.
bool is_alnum_at_byte(std::string_view s, size_t byte_offset, std::string_view extra) noexcept
{
	if (byte_offset >= s.length()) {
		return false;
	}

	unsigned char c = static_cast<unsigned char>(s[byte_offset]);
	if (c < 0x80) {
		return is_alnum(static_cast<char>(c), extra);
	}

	// Multi-byte character sequence
	size_t clen = char_len(c);
	if (clen == 1 || byte_offset + clen > s.length()) {
		return false; // Continuation byte or truncated
	}

	if (!extra.empty()) {
		std::string_view glyph = s.substr(byte_offset, clen);
		return extra.find(glyph) != std::string_view::npos;
	}

	return false;
}

// Returns true if the 0-indexed character at char_pos is alphanumeric or matches extra.
bool is_alnum_at_char(std::string_view s, size_t char_pos, std::string_view extra) noexcept
{
	size_t byte_offset = char_to_byte_offset(s, char_pos);
	return is_alnum_at_byte(s, byte_offset, extra);
}

// Returns true if s is non-empty and every character in s satisfies is_alnum with extra.
bool is_alnum_only(std::string_view s, std::string_view extra) noexcept
{
	if (s.empty()) {
		return false;
	}

	size_t offset = 0;
	while (offset < s.length()) {
		unsigned char c = static_cast<unsigned char>(s[offset]);
		if (c < 0x80) {
			if (!is_alnum(static_cast<char>(c), extra)) {
				return false;
			}
			offset++;
		} else {
			size_t clen = char_len(c);
			if (clen == 1 || offset + clen > s.length()) {
				return false; // Malformed or truncated
			}
			if (extra.empty()) {
				return false;
			}
			std::string_view glyph = s.substr(offset, clen);
			if (extra.find(glyph) == std::string_view::npos) {
				return false;
			}
			offset += clen;
		}
	}
	return true;
}

// Replaces invalid UTF-8 sequences (invalid lead bytes, incomplete sequences,
// or non-conforming continuation bytes) with replacement character '?'.
// Preserves all valid UTF-8 sequences and ASCII characters unchanged.
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

// Validates whether string s contains strictly valid UTF-8 sequences.
// Returns false if any embedded null bytes (0x00), invalid lead bytes (0x80..0xBF, 0xF8..0xFF),
// truncated multi-byte sequences, or malformed continuation bytes are present.
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

// Returns a subview of s with leading whitespace removed without memory allocations.
// Removes ASCII whitespace (std::isspace) and Unicode space characters.
// Safely advances by entire UTF-8 character byte lengths.
std::string_view trim_view_leading(std::string_view s) noexcept
{
	size_t start = 0;
	while (start < s.length() && is_space_at_byte(s, start)) {
		unsigned char c = static_cast<unsigned char>(s[start]);
		start += char_len(c);
	}
	if (start >= s.length()) {
		return {};
	}
	return s.substr(start);
}

// Returns a subview of s with trailing whitespace removed without memory allocations.
// Removes ASCII whitespace (std::isspace) and Unicode space characters.
// Safely steps back to preceding character boundaries and removes entire multi-byte characters.
std::string_view trim_view_trailing(std::string_view s) noexcept
{
	size_t end = s.length();
	while (end > 0) {
		size_t prev = end - 1;
		while (prev > 0 && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) {
			prev--;
		}
		unsigned char lead = static_cast<unsigned char>(s[prev]);
		size_t clen = char_len(lead);
		if (prev + clen == end && is_space_at_byte(s, prev)) {
			end = prev;
		} else {
			break;
		}
	}
	return s.substr(0, end);
}

// Returns a subview of s with both leading and trailing whitespace removed without memory allocations.
// Removes ASCII whitespace (std::isspace) and Unicode space characters.
std::string_view trim_view(std::string_view s) noexcept
{
	size_t start = 0;
	while (start < s.length() && is_space_at_byte(s, start)) {
		unsigned char c = static_cast<unsigned char>(s[start]);
		start += char_len(c);
	}
	if (start >= s.length()) {
		return {};
	}

	size_t end = s.length();
	while (end > start) {
		size_t prev = end - 1;
		while (prev > start && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) {
			prev--;
		}
		unsigned char lead = static_cast<unsigned char>(s[prev]);
		size_t clen = char_len(lead);
		if (prev + clen == end && is_space_at_byte(s, prev)) {
			end = prev;
		} else {
			break;
		}
	}
	return s.substr(start, end - start);
}

// Returns a copy of s with leading whitespace removed.
std::string trim_leading_whitespace(std::string_view s)
{
	return std::string(trim_view_leading(s));
}

// Returns a copy of s with trailing whitespace removed.
std::string trim_trailing_whitespace(std::string_view s)
{
	return std::string(trim_view_trailing(s));
}

// Returns a copy of s with both leading and trailing whitespace removed.
std::string trim_full(std::string_view s)
{
	return std::string(trim_view(s));
}

// Convenience alias for trim_full.
std::string trim(std::string_view s)
{
	return trim_full(s);
}

// Returns true if s is empty or contains only whitespace (ASCII whitespace and Unicode space characters).
// Evaluates without memory allocations.
bool is_whitespace_only(std::string_view s) noexcept
{
	return trim_view(s).empty();
}

// Wraps text to a target display width. Prepends prefix to line 1 and indents subsequent
// wrapped lines with spaces matching the visual display width of prefix.
// Word boundaries (spaces, tabs) are preferred for line breaks; if a single word exceeds
// the available width, it is split across lines according to character display width.
// If width is narrower than prefix_width + 4, text is returned as a single unwrapped line.
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

// Detects the MIME content type of buffer via libmagic inspection.
std::string detect_mime(std::string_view buffer)
{
	return mime::detect_buffer_type(buffer);
}

// Strips ANSI escape sequences (CSI commands, OSC window/icon commands, 2-byte sequences)
// from terminal output streams to prevent formatting codes leaking into logs or LLM context.
// Handles both raw ESC bytes (\x1b) and literal string representations ("\\x1b", "\\u001b").
// Incomplete trailing escape sequences (e.g. at buffer truncation boundaries) are stripped.
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

// Configures the globally active parameter naming style for MCP tool schema advertising
// and markdown table header formatting.
void set_active_param_case_style(param_case_style style) noexcept
{
	g_active_param_case_style.store(style, std::memory_order_relaxed);
}

// Queries the globally active parameter naming style.
param_case_style get_active_param_case_style() noexcept
{
	return g_active_param_case_style.load(std::memory_order_relaxed);
}

// Formats a canonical snake_case parameter name to match the active parameter case style:
// - snake_case: returned as-is (e.g. "start_line")
// - pascal_case: converted via to_PascalCase (e.g. "StartLine")
// - camel_case: converted via to_camelCase (e.g. "startLine")
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

// Formats column header names in a markdown table header line (e.g. "| Symbol | start_line | end_line |\n").
// Identifiers starting with a lowercase character are treated as parameter column names and
// transformed to match the active parameter case style, while capitalized title columns (e.g. "Symbol",
// "Type", "Kind") and markdown structural characters are preserved.
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
