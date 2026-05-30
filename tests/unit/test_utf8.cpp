#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include "utf8.h"

void test_utf8_char_len()
{
	assert(utf8::char_len('a') == 1);
	// 2-byte leading byte (0xC0 mask)
	assert(utf8::char_len(0xC0) == 2);
	assert(utf8::char_len(0xDF) == 2);
	// 3-byte leading byte (0xE0 mask)
	assert(utf8::char_len(0xE0) == 3);
	assert(utf8::char_len(0xEF) == 3);
	// 4-byte leading byte (0xF0 mask)
	assert(utf8::char_len(0xF0) == 4);
	assert(utf8::char_len(0xF7) == 4);
}

void test_utf8_length()
{
	assert(utf8::length("abc") == 3);
	assert(utf8::length("") == 0);
	// "🦀" is 4 bytes, "abc" is 3 bytes, total 4 characters
	assert(utf8::length("🦀abc") == 4);
	// UTF-8 flag emoji "🇺🇸" is composed of 2 regional indicator characters (each 4 bytes, total 8 bytes)
	assert(utf8::length("🇺🇸") == 2);
}

void test_utf8_char_to_byte_offset()
{
	std::string s = "🦀abc";
	assert(utf8::char_to_byte_offset(s, 0) == 0);
	assert(utf8::char_to_byte_offset(s, 1) == 4); // After "🦀" (4 bytes)
	assert(utf8::char_to_byte_offset(s, 2) == 5); // After "🦀a"
	assert(utf8::char_to_byte_offset(s, 4) == 7); // After whole string
	assert(utf8::char_to_byte_offset(s, 10) == 7); // Out of bounds fallback
}

void test_utf8_byte_to_char_pos()
{
	std::string s = "🦀abc";
	assert(utf8::byte_to_char_pos(s, 0) == 0);
	assert(utf8::byte_to_char_pos(s, 1) == 1); // inside first char -> maps to 1
	assert(utf8::byte_to_char_pos(s, 4) == 1); // exactly after first char
	assert(utf8::byte_to_char_pos(s, 5) == 2); // exactly after "🦀a"
	assert(utf8::byte_to_char_pos(s, 7) == 4);
	assert(utf8::byte_to_char_pos(s, 100) == 4);
}

void test_utf8_next_character()
{
	std::string s = "🦀a";
	size_t offset = 0;
	std::string c;

	assert(utf8::next_character(s, offset, c));
	assert(offset == 4);
	assert(c == "🦀");

	assert(utf8::next_character(s, offset, c));
	assert(offset == 5);
	assert(c == "a");

	assert(!utf8::next_character(s, offset, c));
}

int main()
{
	test_utf8_char_len();
	test_utf8_length();
	test_utf8_char_to_byte_offset();
	test_utf8_byte_to_char_pos();
	test_utf8_next_character();

	std::cout << "All UTF-8 unit tests passed!" << std::endl;
	return 0;
}
