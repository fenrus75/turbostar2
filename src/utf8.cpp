#include "utf8.h"
#include <wchar.h>
#include "tools/magic_compat.h"

namespace utf8
{

size_t char_len(unsigned char lead_byte)
{
	if (lead_byte < 0x80)
		return 1;
	if ((lead_byte & 0xE0) == 0xC0)
		return 2;
	if ((lead_byte & 0xF0) == 0xE0)
		return 3;
	if ((lead_byte & 0xF8) == 0xF0)
		return 4;
	return 1; // Invalid UTF-8 sequence, treat as 1 byte to make progress
}

size_t length(std::string_view s)
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

size_t display_width(std::string_view s)
{
	size_t total_width = 0;
	size_t offset = 0;
	mbstate_t state = {};
	while (offset < s.length()) {
		wchar_t wc;
		size_t res = mbrtowc(&wc, s.data() + offset, s.length() - offset, &state);
		if (res == (size_t)-1 || res == (size_t)-2) {
			// Invalid or incomplete char, assume width 1 and advance 1 byte
			total_width += 1;
			offset += 1;
		} else if (res == 0) {
			// NUL byte, width 0
			offset += 1;
		} else {
			int w = wcwidth(wc);
			if (w >= 0) {
				total_width += w;
			}
			offset += res;
		}
	}
	return total_width;
}

size_t char_to_byte_offset(std::string_view s, size_t char_pos)
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

size_t byte_to_char_pos(std::string_view s, size_t byte_offset)
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

std::vector<std::string> wrap_string(const std::string &prefix, const std::string &text, int width)
{
	std::vector<std::string> lines;
	int prefix_utf8_len = static_cast<int>(utf8::display_width(prefix));

	if (width <= prefix_utf8_len + 4) {
		lines.push_back(prefix + text);
		return lines;
	}

	int available_width = width - prefix_utf8_len;
	std::string current_prefix = prefix;
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

		lines.push_back(current_prefix + text.substr(byte_idx, chunk_byte_len));
		byte_idx += chunk_byte_len;

		while (byte_idx < text.length() && (text[byte_idx] == ' ' || text[byte_idx] == '\t')) {
			byte_idx++;
		}

		current_prefix = std::string(prefix_utf8_len, ' ');
	}

	if (lines.empty()) {
		lines.push_back(prefix);
	}

	return lines;
}

std::string detect_mime(std::string_view buffer)
{
	std::string mime_type = "";
	magic_t magic = magic_open(MAGIC_MIME_TYPE);
	if (magic) {
		if (magic_load(magic, nullptr) == 0) {
			const char *mime = magic_buffer(magic, buffer.data(), buffer.size());
			if (mime) {
				mime_type = mime;
			}
		}
		magic_close(magic);
	}
	return mime_type;
}

} // namespace utf8
