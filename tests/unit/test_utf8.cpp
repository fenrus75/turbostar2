#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <clocale>
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

void test_utf8_wrap_string()
{
	// 1. Basic wrapping with no prefix
	std::string text1 = "Quick brown fox jumps over the lazy dog";
	// Available width = 15. Wraps at spaces.
	auto lines1 = utf8::wrap_string("", text1, 15);
	assert(lines1.size() == 3);
	assert(lines1[0] == "Quick brown fox");
	assert(lines1[1] == "jumps over the");
	assert(lines1[2] == "lazy dog");

	// 2. Wrapping with a prefix (e.g. comment characters)
	std::string text2 = "This is a comment line that should wrap nicely.";
	// Prefix = "// ", width = 25. prefix_len = 3. available = 22.
	auto lines2 = utf8::wrap_string("// ", text2, 25);
	assert(lines2.size() == 3);
	assert(lines2[0] == "// This is a comment line");
	assert(lines2[1] == "   that should wrap");
	assert(lines2[2] == "   nicely.");

	// 3. Wrapping with UTF-8 double-width characters
	std::string text3 = "螃蟹 螃蟹 螃蟹 螃蟹 螃蟹"; // each 🦀/Chinese char is width 2
	// Width = 11. Prefix = "* ". prefix_len = 2. available = 9.
	// "螃蟹" is display width 4. "螃蟹 螃蟹" is display width 9.
	auto lines3 = utf8::wrap_string("* ", text3, 11);
	assert(lines3.size() == 3);
	assert(lines3[0] == "* 螃蟹 螃蟹");
	assert(lines3[1] == "  螃蟹 螃蟹");
	assert(lines3[2] == "  螃蟹");
}

void test_utf8_ascii_fastpath()
{
	std::string_view ascii_str = "Hello, Turbostar Editor 2026!";
	assert(utf8::display_width(ascii_str) == ascii_str.length());
	assert(utf8::length(ascii_str) == ascii_str.length());
	assert(utf8::is_valid_utf8(ascii_str));

	std::string_view mixed_str = "Hello 🦀 World!";
	assert(utf8::display_width(mixed_str) == 15); // 6 ASCII + 2 for crab emoji + 7 ASCII
	assert(utf8::length(mixed_str) == 14); // 6 ASCII + 1 crab emoji + 7 ASCII
}

int main()
{
	test_watchdog::setup_watchdog(30);
	std::setlocale(LC_ALL, "");
	test_utf8_char_len();
	test_utf8_length();
	test_utf8_char_to_byte_offset();
	test_utf8_byte_to_char_pos();
	test_utf8_next_character();
	test_utf8_wrap_string();
	test_utf8_ascii_fastpath();

	std::cout << "All UTF-8 unit tests passed!" << std::endl;
	return 0;
}
