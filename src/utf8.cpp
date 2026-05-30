#include "utf8.h"

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

} // namespace utf8
