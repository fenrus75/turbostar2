#include "utf8.h"
#include <wchar.h>
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

} // namespace utf8
