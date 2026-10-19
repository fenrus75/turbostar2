// Tested source file: src/utf8.cpp
#include "test_watchdog.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <clocale>
#include "utf8.h"

void test_utf8_char_len()
{
	// 1-byte ASCII (0x00 - 0x7F)
	assert(utf8::char_len(0x00) == 1);
	assert(utf8::char_len('a') == 1);
	assert(utf8::char_len('Z') == 1);
	assert(utf8::char_len('9') == 1);
	assert(utf8::char_len(' ') == 1);
	assert(utf8::char_len(0x7F) == 1);

	// 2-byte leading byte (0xC0 - 0xDF)
	assert(utf8::char_len(0xC0) == 2);
	assert(utf8::char_len(0xC2) == 2);
	assert(utf8::char_len(0xDF) == 2);

	// 3-byte leading byte (0xE0 - 0xEF)
	assert(utf8::char_len(0xE0) == 3);
	assert(utf8::char_len(0xE2) == 3);
	assert(utf8::char_len(0xEF) == 3);

	// 4-byte leading byte (0xF0 - 0xF7)
	assert(utf8::char_len(0xF0) == 4);
	assert(utf8::char_len(0xF4) == 4);
	assert(utf8::char_len(0xF7) == 4);

	// Continuation bytes (0x80 - 0xBF) treated as invalid lead bytes -> 1 byte fallback
	assert(utf8::char_len(0x80) == 1);
	assert(utf8::char_len(0x9F) == 1);
	assert(utf8::char_len(0xBF) == 1);

	// Legacy / invalid lead bytes (0xF8 - 0xFF) -> 1 byte fallback
	assert(utf8::char_len(0xF8) == 1);
	assert(utf8::char_len(0xFC) == 1);
	assert(utf8::char_len(0xFE) == 1);
	assert(utf8::char_len(0xFF) == 1);
}

void test_utf8_length()
{
	// Empty string
	assert(utf8::length("") == 0);

	// Pure ASCII
	assert(utf8::length("a") == 1);
	assert(utf8::length("abc") == 3);
	assert(utf8::length("Hello, World!") == 13);

	// 2-byte sequences (Latin-1 supplement, Cyrillic, Greek)
	assert(utf8::length("café") == 4);   // 5 bytes, 4 chars
	assert(utf8::length("Привет") == 6); // 12 bytes, 6 chars
	assert(utf8::length("Ελλάδα") == 6); // 12 bytes, 6 chars

	// 3-byte sequences (CJK, Euro sign)
	assert(utf8::length("€100") == 4);   // 6 bytes, 4 chars
	assert(utf8::length("日本語") == 3);  // 9 bytes, 3 chars
	assert(utf8::length("中文测试") == 4); // 12 bytes, 4 chars

	// 4-byte sequences (Emojis, regional indicators)
	assert(utf8::length("🦀") == 1);         // 4 bytes, 1 char
	assert(utf8::length("🦀abc") == 4);
	assert(utf8::length("🦀🚀✨🔥") == 4);     // 16 bytes, 4 chars
	assert(utf8::length("🇺🇸") == 2);         // 2 regional indicator codepoints, 8 bytes

	// Mixed scripts with whitespace
	assert(utf8::length("Hello 世界 🦀 123") == 14);

	// Truncated multi-byte sequence at end of string does not read out of bounds
	assert(utf8::length("abc\xE2\x82") == 4); // 3 ASCII + 1 truncated 3-byte char
	assert(utf8::length("\xF0\x9F") == 1);    // 1 truncated 4-byte char

	// Lone continuation bytes counted as individual characters
	assert(utf8::length("\x80\x81") == 2);
}

void test_utf8_char_to_byte_offset()
{
	// Empty string
	assert(utf8::char_to_byte_offset("", 0) == 0);
	assert(utf8::char_to_byte_offset("", 5) == 0);

	// Pure ASCII string (1:1 mapping)
	std::string ascii = "abcdef";
	for (size_t i = 0; i <= ascii.length(); ++i) {
		assert(utf8::char_to_byte_offset(ascii, i) == i);
	}
	assert(utf8::char_to_byte_offset(ascii, 100) == ascii.length());

	// Simple mixed string
	std::string s1 = "🦀abc";
	assert(utf8::char_to_byte_offset(s1, 0) == 0);
	assert(utf8::char_to_byte_offset(s1, 1) == 4); // After "🦀" (4 bytes)
	assert(utf8::char_to_byte_offset(s1, 2) == 5); // After "🦀a"
	assert(utf8::char_to_byte_offset(s1, 3) == 6); // After "🦀ab"
	assert(utf8::char_to_byte_offset(s1, 4) == 7); // After whole string
	assert(utf8::char_to_byte_offset(s1, 10) == 7); // Out of bounds fallback

	// Complex multi-script string: "A_é_€_🦀_Z"
	// Lengths: 'A'(1) + '_'(1) + 'é'(2) + '_'(1) + '€'(3) + '_'(1) + '🦀'(4) + '_'(1) + 'Z'(1) = 15 bytes, 9 chars
	std::string s2 = "A_é_€_🦀_Z";
	assert(utf8::length(s2) == 9);
	assert(s2.length() == 15);
	assert(utf8::char_to_byte_offset(s2, 0) == 0);
	assert(utf8::char_to_byte_offset(s2, 1) == 1);
	assert(utf8::char_to_byte_offset(s2, 2) == 2);
	assert(utf8::char_to_byte_offset(s2, 3) == 4);  // after 'é' (2 bytes)
	assert(utf8::char_to_byte_offset(s2, 4) == 5);
	assert(utf8::char_to_byte_offset(s2, 5) == 8);  // after '€' (3 bytes)
	assert(utf8::char_to_byte_offset(s2, 6) == 9);
	assert(utf8::char_to_byte_offset(s2, 7) == 13); // after '🦀' (4 bytes)
	assert(utf8::char_to_byte_offset(s2, 8) == 14);
	assert(utf8::char_to_byte_offset(s2, 9) == 15);
	assert(utf8::char_to_byte_offset(s2, 20) == 15);
}

void test_utf8_byte_to_char_pos()
{
	// Empty string
	assert(utf8::byte_to_char_pos("", 0) == 0);
	assert(utf8::byte_to_char_pos("", 10) == 0);

	// Pure ASCII string (1:1 mapping)
	std::string ascii = "abcdef";
	for (size_t i = 0; i <= ascii.length(); ++i) {
		assert(utf8::byte_to_char_pos(ascii, i) == i);
	}
	assert(utf8::byte_to_char_pos(ascii, 100) == ascii.length());

	// Simple mixed string
	std::string s1 = "🦀abc";
	assert(utf8::byte_to_char_pos(s1, 0) == 0);
	assert(utf8::byte_to_char_pos(s1, 1) == 1); // inside first char -> maps to 1
	assert(utf8::byte_to_char_pos(s1, 2) == 1);
	assert(utf8::byte_to_char_pos(s1, 3) == 1);
	assert(utf8::byte_to_char_pos(s1, 4) == 1); // exactly after first char
	assert(utf8::byte_to_char_pos(s1, 5) == 2); // exactly after "🦀a"
	assert(utf8::byte_to_char_pos(s1, 7) == 4);
	assert(utf8::byte_to_char_pos(s1, 100) == 4);

	// Complex multi-script string: "A_é_€_🦀_Z"
	std::string s2 = "A_é_€_🦀_Z";
	// Bidirectional round-trip identity at exact character boundaries
	for (size_t char_idx = 0; char_idx <= utf8::length(s2); ++char_idx) {
		size_t byte_off = utf8::char_to_byte_offset(s2, char_idx);
		assert(utf8::byte_to_char_pos(s2, byte_off) == char_idx);
	}

	// Inside multi-byte sequences maps to completed character
	assert(utf8::byte_to_char_pos(s2, 3) == 3);  // inside 'é' (bytes 2-3) -> maps to char 3
	assert(utf8::byte_to_char_pos(s2, 6) == 5);  // inside '€' (bytes 5-7) -> maps to char 5
	assert(utf8::byte_to_char_pos(s2, 10) == 7); // inside '🦀' (bytes 9-12) -> maps to char 7
	assert(utf8::byte_to_char_pos(s2, 12) == 7);

	// Truncated multi-byte sequence at end of string
	assert(utf8::byte_to_char_pos("X\xE2\x82", 3) == 2);
}

void test_utf8_next_character()
{
	// Empty string
	std::string c;
	size_t offset = 0;
	assert(!utf8::next_character("", offset, c));
	assert(c.empty());
	assert(offset == 0);

	// Out of bounds offset
	std::string s1 = "abc";
	offset = 10;
	assert(!utf8::next_character(s1, offset, c));
	assert(c.empty());

	// Iteration across 1, 2, 3, 4-byte characters: "Aé€🦀"
	std::string s2 = "Aé€🦀";
	offset = 0;

	assert(utf8::next_character(s2, offset, c));
	assert(offset == 1);
	assert(c == "A");

	assert(utf8::next_character(s2, offset, c));
	assert(offset == 3); // 1 + 2
	assert(c == "é");

	assert(utf8::next_character(s2, offset, c));
	assert(offset == 6); // 3 + 3
	assert(c == "€");

	assert(utf8::next_character(s2, offset, c));
	assert(offset == 10); // 6 + 4
	assert(c == "🦀");

	assert(!utf8::next_character(s2, offset, c));
	assert(c.empty());

	// Truncated multi-byte sequence at end of string
	std::string s_trunc = "X\xE2\x82"; // 'X' followed by truncated 3-byte prefix
	offset = 0;
	assert(utf8::next_character(s_trunc, offset, c));
	assert(offset == 1);
	assert(c == "X");

	assert(utf8::next_character(s_trunc, offset, c));
	assert(offset == 3);
	assert(c == "\xE2\x82");

	assert(!utf8::next_character(s_trunc, offset, c));
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

	// 4. Narrow width threshold (width <= prefix_width + 4): returns single unwrapped line
	auto lines_narrow = utf8::wrap_string("// ", "short text", 7); // prefix len 3 + 4 = 7
	assert(lines_narrow.size() == 1);
	assert(lines_narrow[0] == "// short text");

	// 5. Empty text
	auto lines_empty = utf8::wrap_string("// ", "", 25);
	assert(lines_empty.size() == 1);
	assert(lines_empty[0] == "// ");

	auto lines_both_empty = utf8::wrap_string("", "", 25);
	assert(lines_both_empty.size() == 1);
	assert(lines_both_empty[0] == "");

	// 6. Long word without spaces exceeding available width splits cleanly across lines
	std::string long_word = "abcdefghijklmnopqrstuvwxyz";
	auto lines_long = utf8::wrap_string("", long_word, 10);
	assert(lines_long.size() == 3);
	assert(lines_long[0] == "abcdefghij");
	assert(lines_long[1] == "klmnopqrst");
	assert(lines_long[2] == "uvwxyz");

	// 7. Multi-byte emojis wrapping without splitting bytes
	std::string emoji_text = "🦀 🦀 🦀 🦀";
	auto lines_emoji = utf8::wrap_string("", emoji_text, 5); // each emoji is width 2 + space = 5
	assert(lines_emoji.size() == 2);
	assert(lines_emoji[0] == "🦀 🦀");
	assert(lines_emoji[1] == "🦀 🦀");

	// 8. Truncated multi-byte sequence in wrapped text
	auto lines_trunc = utf8::wrap_string("", "hello \xC2 world", 10);
	assert(lines_trunc.size() >= 1);
	auto lines_trunc_end = utf8::wrap_string("", "hello \xC2", 10);
	assert(lines_trunc_end.size() >= 1);
}

void test_utf8_ascii_fastpath()
{
	// Empty string display width
	assert(utf8::display_width("") == 0);

	// ASCII string
	std::string_view ascii_str = "Hello, Turbostar Editor 2026!";
	assert(utf8::display_width(ascii_str) == ascii_str.length());
	assert(utf8::length(ascii_str) == ascii_str.length());

	// Invalid / truncated multi-byte sequences in display_width
	assert(utf8::display_width("bad\xFF\xFE") == 5);
	assert(utf8::display_width("trunc\xC2") == 6);
	assert(utf8::is_valid_utf8(ascii_str));

	// Accented Latin characters (display width 1 each)
	assert(utf8::display_width("café") == 4);
	assert(utf8::display_width("naïve") == 5);
	assert(utf8::display_width("München") == 7);

	// CJK East Asian Fullwidth characters (display width 2 each)
	assert(utf8::display_width("你好") == 4);
	assert(utf8::display_width("日本語") == 6);
	assert(utf8::display_width("螃蟹") == 4);

	// Emoji (display width 2 each)
	assert(utf8::display_width("🦀") == 2);
	assert(utf8::display_width("🚀") == 2);
	assert(utf8::display_width("✨") == 2);

	// Mixed ASCII, CJK, and Emoji
	std::string_view mixed_str = "Hello 🦀 World!";
	assert(utf8::display_width(mixed_str) == 15); // 6 ASCII + 2 crab + 7 ASCII
	assert(utf8::length(mixed_str) == 14);

	std::string_view cjk_mixed = "Code: 螃蟹 100%";
	assert(utf8::display_width(cjk_mixed) == 15); // 6 ASCII + 4 CJK + 5 ASCII
	assert(utf8::length(cjk_mixed) == 13);

	// Control / embedded NUL byte
	std::string nul_embedded = std::string("a\0b", 3);
	assert(utf8::display_width(nul_embedded) == 3);
}

void test_utf8_sanitize_and_validation()
{
	// 1. is_valid_utf8
	// Valid strings
	assert(utf8::is_valid_utf8(""));
	assert(utf8::is_valid_utf8("Simple ASCII string 123"));
	assert(utf8::is_valid_utf8("Hello 🦀 World"));
	assert(utf8::is_valid_utf8("Café, München, Résumé"));
	assert(utf8::is_valid_utf8("日本語 中文 Русский Ελληνικά"));

	// Invalid: embedded NUL byte
	std::string nul_str = std::string("Hello\0World", 11);
	assert(!utf8::is_valid_utf8(nul_str));
	std::string trailing_nul = std::string("end\0", 4);
	assert(!utf8::is_valid_utf8(trailing_nul));

	// Invalid: lone continuation bytes (0x80 - 0xBF)
	assert(!utf8::is_valid_utf8("\x80"));
	assert(!utf8::is_valid_utf8("\xBF"));
	assert(!utf8::is_valid_utf8("prefix\x80postfix"));
	assert(!utf8::is_valid_utf8("\x80\x81\x82"));

	// Invalid: invalid lead bytes (0xF8-0xFF)
	assert(!utf8::is_valid_utf8("\xF8\x80\x80\x80\x80"));
	assert(!utf8::is_valid_utf8("\xFF\xFE"));

	// Invalid: truncated multi-byte sequence at end of string
	assert(!utf8::is_valid_utf8("test\xC3"));             // missing 1 byte of 2-byte char
	assert(!utf8::is_valid_utf8("test\xE2\x82"));         // missing 1 byte of 3-byte char
	assert(!utf8::is_valid_utf8("test\xF0\x9F\xA6"));     // missing 1 byte of 4-byte char

	// Invalid: malformed continuation byte
	assert(!utf8::is_valid_utf8("\xC3\x20"));             // continuation replaced with space
	assert(!utf8::is_valid_utf8("\xE2\x28\xA1"));         // invalid byte 2 of 3-byte char
	assert(!utf8::is_valid_utf8("\xE2\x82\x28"));         // invalid byte 3 of 3-byte char
	assert(!utf8::is_valid_utf8("\xF0\x28\x80\x80"));     // invalid byte 2 of 4-byte char

	// 2. sanitize
	// Already valid UTF-8 unchanged
	assert(utf8::sanitize("") == "");
	assert(utf8::sanitize("Hello 🦀 World") == "Hello 🦀 World");
	assert(utf8::sanitize("café au lait") == "café au lait");

	// Invalid bytes replaced with '?'
	assert(utf8::sanitize("Hello \xFF\xFE World") == "Hello ?? World");
	assert(utf8::sanitize("\x80lone continuation\xBF") == "?lone continuation?");
	assert(utf8::sanitize("truncated 2-byte: \xC3") == "truncated 2-byte: ?");
	assert(utf8::sanitize("truncated 3-byte: \xE2\x82") == "truncated 3-byte: ??");
	assert(utf8::sanitize("truncated 4-byte: \xF0\x9F\xA6") == "truncated 4-byte: ???");
	assert(utf8::sanitize("bad continuation: \xC3" "\x20" "end") == "bad continuation: ? end");
}

void test_utf8_trim_and_terminal_sanitize()
{
	// 1. trim and trim_full basic ASCII
	assert(utf8::trim("") == "");
	assert(utf8::trim_full("") == "");
	assert(utf8::trim("   \t\r\n\v\f  ") == "");
	assert(utf8::trim_full("   \t\r\n\v\f  ") == "");
	assert(utf8::trim("hello world") == "hello world");
	assert(utf8::trim_full("hello world") == "hello world");
	assert(utf8::trim("   hello world") == "hello world");
	assert(utf8::trim("hello world   \n\t") == "hello world");
	assert(utf8::trim(" \r\n\t  hello world   \t\r\n ") == "hello world");
	assert(utf8::trim("  interior   spaces   kept  ") == "interior   spaces   kept");

	// 2. trim_leading_whitespace
	assert(utf8::trim_leading_whitespace("") == "");
	assert(utf8::trim_leading_whitespace("   \t\r\n   ") == "");
	assert(utf8::trim_leading_whitespace("   hello world") == "hello world");
	assert(utf8::trim_leading_whitespace("hello world   ") == "hello world   ");
	assert(utf8::trim_leading_whitespace("   hello world   ") == "hello world   ");
	assert(utf8::trim_leading_whitespace("  interior   spaces  ") == "interior   spaces  ");

	// 3. trim_trailing_whitespace
	assert(utf8::trim_trailing_whitespace("") == "");
	assert(utf8::trim_trailing_whitespace("   \t\r\n   ") == "");
	assert(utf8::trim_trailing_whitespace("   hello world") == "   hello world");
	assert(utf8::trim_trailing_whitespace("hello world   ") == "hello world");
	assert(utf8::trim_trailing_whitespace("   hello world   ") == "   hello world");
	assert(utf8::trim_trailing_whitespace("  interior   spaces  ") == "  interior   spaces");

	// 4. Unicode whitespace trimming
	const std::string nbsp = "\xC2\xA0";
	const std::string ogham = "\xE1\x9A\x80";
	const std::string quad = "\xE2\x80\x80";
	const std::string em_space = "\xE2\x80\x83";
	const std::string thin_space = "\xE2\x80\x89";
	const std::string hair_space = "\xE2\x80\x8A";
	const std::string line_sep = "\xE2\x80\xA8";
	const std::string para_sep = "\xE2\x80\xA9";
	const std::string n_nbsp = "\xE2\x80\xAF";
	const std::string math_sp = "\xE2\x81\x9F";
	const std::string cjk_space = "\xE3\x80\x80";

	// All Unicode whitespace only
	std::string all_unicode_spaces = nbsp + ogham + quad + em_space + thin_space + hair_space +
	                                 line_sep + para_sep + n_nbsp + math_sp + cjk_space;
	assert(utf8::trim(all_unicode_spaces) == "");
	assert(utf8::trim_full(all_unicode_spaces) == "");
	assert(utf8::trim_leading_whitespace(all_unicode_spaces) == "");
	assert(utf8::trim_trailing_whitespace(all_unicode_spaces) == "");

	// Mixed ASCII and Unicode spaces surrounding ASCII text
	std::string surrounded = " \t" + nbsp + quad + "hello world" + em_space + cjk_space + "\r\n ";
	assert(utf8::trim_full(surrounded) == "hello world");
	assert(utf8::trim(surrounded) == "hello world");
	assert(utf8::trim_leading_whitespace(surrounded) == "hello world" + em_space + cjk_space + "\r\n ");
	assert(utf8::trim_trailing_whitespace(surrounded) == " \t" + nbsp + quad + "hello world");

	// Multi-byte Unicode content with Unicode whitespace
	std::string utf8_text = "こんにちは世界";
	std::string cjk_surrounded = cjk_space + cjk_space + utf8_text + cjk_space;
	assert(utf8::trim_full(cjk_surrounded) == utf8_text);
	assert(utf8::trim_leading_whitespace(cjk_surrounded) == utf8_text + cjk_space);
	assert(utf8::trim_trailing_whitespace(cjk_surrounded) == cjk_space + cjk_space + utf8_text);

	// Whole character deletion verification: ensure multibyte characters aren't partially chopped
	std::string mixed_nbsp = nbsp + "Café" + nbsp;
	assert(utf8::trim_full(mixed_nbsp) == "Café");
	assert(utf8::trim_leading_whitespace(mixed_nbsp) == "Café" + nbsp);
	assert(utf8::trim_trailing_whitespace(mixed_nbsp) == nbsp + "Café");

	// Internal Unicode whitespace must NOT be removed
	std::string interior_unicode = "Café" + nbsp + "Au" + cjk_space + "Lait";
	assert(utf8::trim_full(interior_unicode) == interior_unicode);
	assert(utf8::trim_full(nbsp + interior_unicode + nbsp) == interior_unicode);

	// 5. False positive resistance (similar lead bytes that are NOT spaces)
	// \xC2\xA1 (¡) must NOT be trimmed
	std::string inv_excl = "\xC2\xA1Hello\xC2\xA1";
	assert(utf8::trim_full(inv_excl) == inv_excl);
	assert(utf8::trim_leading_whitespace(inv_excl) == inv_excl);
	assert(utf8::trim_trailing_whitespace(inv_excl) == inv_excl);

	// \xE2\x80\x8B (Zero-width space) must NOT be trimmed
	std::string zwsp = "\xE2\x80\x8Btext\xE2\x80\x8B";
	assert(utf8::trim_full(zwsp) == zwsp);

	// \xE3\x80\x81 (Ideographic comma 、) must NOT be trimmed
	std::string cjk_comma = "\xE3\x80\x81text\xE3\x80\x81";
	assert(utf8::trim_full(cjk_comma) == cjk_comma);

	// Truncated multi-byte lead bytes at ends
	assert(utf8::trim_full("\xC2") == "\xC2");
	assert(utf8::trim_full("\xE2\x80") == "\xE2\x80");

	// 6. is_whitespace_only
	assert(utf8::is_whitespace_only(""));
	assert(utf8::is_whitespace_only("   \t\r\n\v\f  "));
	assert(utf8::is_whitespace_only(all_unicode_spaces));
	assert(utf8::is_whitespace_only("  \t" + nbsp + quad + "\r\n " + cjk_space));
	assert(!utf8::is_whitespace_only("a"));
	assert(!utf8::is_whitespace_only("   a   "));
	assert(!utf8::is_whitespace_only(surrounded));
	assert(!utf8::is_whitespace_only("Café"));
	assert(!utf8::is_whitespace_only(zwsp));
	assert(!utf8::is_whitespace_only(inv_excl));
	assert(!utf8::is_whitespace_only("\xC2"));
	assert(!utf8::is_whitespace_only("\xE2\x80"));

	// Invariance check: is_whitespace_only(s) == trim_full(s).empty()
	for (const auto &s : {
	         std::string(""),
	         std::string(" "),
	         std::string("   \t\r\n  "),
	         all_unicode_spaces,
	         surrounded,
	         mixed_nbsp,
	         utf8_text,
	         zwsp,
	         cjk_comma,
	         std::string("non-space")
	     }) {
		assert(utf8::is_whitespace_only(s) == utf8::trim_full(s).empty());
		assert(utf8::trim_view(s) == utf8::trim_full(s));
		assert(utf8::trim_view_leading(s) == utf8::trim_leading_whitespace(s));
		assert(utf8::trim_view_trailing(s) == utf8::trim_trailing_whitespace(s));
		std::string_view tv = utf8::trim_view(s);
		if (!tv.empty()) {
			assert(tv.data() >= s.data() && (tv.data() + tv.size()) <= (s.data() + s.size()));
		}
	}

	// 3. sanitize_terminal_output
	assert(utf8::sanitize_terminal_output("") == "");
	assert(utf8::sanitize_terminal_output("Pure plain text") == "Pure plain text");

	// ANSI CSI SGR color and styling codes
	std::string ansi_colored = "\x1b[31mRed Error\x1b[0m Normal Text";
	assert(utf8::sanitize_terminal_output(ansi_colored) == "Red Error Normal Text");
	assert(utf8::sanitize_terminal_output("\x1b[1;32;40mBold Green\x1b[0m") == "Bold Green");

	// CSI cursor and screen manipulation
	assert(utf8::sanitize_terminal_output("\x1b[2J\x1b[HCleared") == "Cleared");
	assert(utf8::sanitize_terminal_output("\x1b[?25hVisible Cursor") == "Visible Cursor");
	assert(utf8::sanitize_terminal_output("\x1b[3~Deleted") == "Deleted");
	assert(utf8::sanitize_terminal_output("\x1b[2@Inserted") == "Inserted");

	// Literal string-escaped ANSI representations
	assert(utf8::sanitize_terminal_output("\\x1b[32mSuccess\\x1b[0m") == "Success");
	assert(utf8::sanitize_terminal_output("\\x1B[31mError\\x1B[0m") == "Error");
	assert(utf8::sanitize_terminal_output("\\u001b[33mWarning\\u001b[0m") == "Warning");
	assert(utf8::sanitize_terminal_output("\\u001B[34mInfo\\u001B[0m") == "Info");

	// OSC sequences (window title, hyperlinks) with BEL (\x07) and ST (\x1b\) terminators
	assert(utf8::sanitize_terminal_output("\x1b]0;Turbostar Editor\x07Text") == "Text");
	assert(utf8::sanitize_terminal_output("\x1b]2;Status Window\x1b\\Active") == "Active");

	// 2-byte escape sequences
	assert(utf8::sanitize_terminal_output("\x1b=Keypad\x1b>Normal") == "KeypadNormal");
	assert(utf8::sanitize_terminal_output("\x1b" "7Save\x1b" "8Restore") == "SaveRestore");

	// Incomplete trailing escape sequences stripped cleanly without leaking
	assert(utf8::sanitize_terminal_output("Program crashed\x1b[") == "Program crashed");
	assert(utf8::sanitize_terminal_output("Program crashed\x1b") == "Program crashed");
	assert(utf8::sanitize_terminal_output("Program crashed\x1b[31") == "Program crashed");
	assert(utf8::sanitize_terminal_output("Program crashed\x1b]0;Unterminated") == "Program crashed");

	// 4. detect_mime
	assert(!utf8::detect_mime("").empty());
	std::string plain_text = "Plain text buffer content\n";
	std::string detected = utf8::detect_mime(plain_text);
	assert(!detected.empty());

	// Detect signature bytes for common formats (fallback path in mime)
	std::string png_sig = "\x89PNG\r\n\x1a\n";
	assert(utf8::detect_mime(png_sig) == "image/png");

	std::string jpeg_sig = "\xff\xd8\xff\xe0";
	assert(utf8::detect_mime(jpeg_sig) == "image/jpeg");

	std::string gif_sig = "GIF89a";
	assert(utf8::detect_mime(gif_sig) == "image/gif");
}

void check_case_idempotence(std::string_view s)
{
	std::string snake = utf8::to_snake_case(s);
	if (snake.empty()) {
		assert(utf8::to_PascalCase(snake).empty());
		assert(utf8::to_camelCase(snake).empty());
		assert(utf8::to_snake_case(snake).empty());
		return;
	}

	using conv_fn = std::string (*)(std::string_view);
	struct op_entry {
		std::string_view name;
		conv_fn fn;
	};

	const std::vector<op_entry> ops = {
		{"snake", utf8::to_snake_case},
		{"pascal", utf8::to_PascalCase},
		{"camel", utf8::to_camelCase},
	};

	// Depth 1: to_snake_case(op1(snake)) == snake
	for (const auto &op1 : ops) {
		std::string mid = op1.fn(snake);
		std::string back = utf8::to_snake_case(mid);
		if (back != snake) {
			std::cerr << "Depth 1 case idempotence failure for input '" << s << "':\n"
				  << "  snake: '" << snake << "'\n"
				  << "  after " << op1.name << ": '" << mid << "'\n"
				  << "  back to snake: '" << back << "' (expected '" << snake << "')\n";
			assert(back == snake);
		}
	}

	// Depth 2: to_snake_case(op2(op1(snake))) == snake
	for (const auto &op1 : ops) {
		for (const auto &op2 : ops) {
			std::string mid1 = op1.fn(snake);
			std::string mid2 = op2.fn(mid1);
			std::string back = utf8::to_snake_case(mid2);
			if (back != snake) {
				std::cerr << "Depth 2 case idempotence failure for input '" << s << "':\n"
					  << "  snake: '" << snake << "'\n"
					  << "  after " << op1.name << ": '" << mid1 << "'\n"
					  << "  after " << op2.name << ": '" << mid2 << "'\n"
					  << "  back to snake: '" << back << "' (expected '" << snake << "')\n";
				assert(back == snake);
			}
		}
	}

	// PascalCase idempotence: to_PascalCase(to_PascalCase(snake)) == to_PascalCase(snake)
	// and to_PascalCase(to_camelCase(snake)) == to_PascalCase(snake)
	std::string pascal = utf8::to_PascalCase(snake);
	assert(utf8::to_PascalCase(pascal) == pascal);
	assert(utf8::to_PascalCase(utf8::to_camelCase(snake)) == pascal);
	assert(utf8::to_pascal_case(snake) == pascal);

	// camelCase idempotence: to_camelCase(to_camelCase(snake)) == to_camelCase(snake)
	// and to_camelCase(to_PascalCase(snake)) == to_camelCase(snake)
	std::string camel = utf8::to_camelCase(snake);
	assert(utf8::to_camelCase(camel) == camel);
	assert(utf8::to_camelCase(utf8::to_PascalCase(snake)) == camel);
	assert(utf8::to_camel_case(snake) == camel);
}

void test_utf8_case_conversions()
{
	// to_lower (char and string_view)
	assert(utf8::to_lower('A') == 'a');
	assert(utf8::to_lower('Z') == 'z');
	assert(utf8::to_lower('a') == 'a');
	assert(utf8::to_lower('0') == '0');
	assert(utf8::to_lower('-') == '-');
	assert(utf8::to_lower(static_cast<char>(0xC3)) == static_cast<char>(0xC3));

	assert(utf8::to_lower("") == "");
	assert(utf8::to_lower("hello_world") == "hello_world");
	assert(utf8::to_lower("HELLO WORLD 123") == "hello world 123");
	assert(utf8::to_lower("Extension.CPP") == "extension.cpp");
	assert(utf8::to_lower("Café Au Lait — München €") == "café au lait — münchen €");
	assert(utf8::to_lower("\x80\xFF\xFE") == "\x80\xFF\xFE");

	// to_upper (char and string_view)
	assert(utf8::to_upper('a') == 'A');
	assert(utf8::to_upper('z') == 'Z');
	assert(utf8::to_upper('A') == 'A');
	assert(utf8::to_upper('0') == '0');
	assert(utf8::to_upper('-') == '-');
	assert(utf8::to_upper(static_cast<char>(0xC3)) == static_cast<char>(0xC3));

	assert(utf8::to_upper("") == "");
	assert(utf8::to_upper("HELLO_WORLD") == "HELLO_WORLD");
	assert(utf8::to_upper("hello world 123") == "HELLO WORLD 123");
	assert(utf8::to_upper("extension.cpp") == "EXTENSION.CPP");
	assert(utf8::to_upper("Café Au Lait — München €") == "CAFé AU LAIT — MüNCHEN €");
	assert(utf8::to_upper("\x80\xFF\xFE") == "\x80\xFF\xFE");

	// to_snake_case
	assert(utf8::to_snake_case("") == "");
	assert(utf8::to_snake_case("path") == "path");
	assert(utf8::to_snake_case("start_line") == "start_line");
	assert(utf8::to_snake_case("StartLine") == "start_line");
	assert(utf8::to_snake_case("startLine") == "start_line");
	assert(utf8::to_snake_case("WaitMsBeforeAsync") == "wait_ms_before_async");
	assert(utf8::to_snake_case("URL") == "url");
	assert(utf8::to_snake_case("HTMLParser") == "html_parser");
	assert(utf8::to_snake_case("newX") == "new_x");
	assert(utf8::to_snake_case("utf8Char") == "utf8_char");
	assert(utf8::to_snake_case("kebab-case-name") == "kebab_case_name");

	// to_PascalCase
	assert(utf8::to_PascalCase("") == "");
	assert(utf8::to_PascalCase("path") == "Path");
	assert(utf8::to_PascalCase("start_line") == "StartLine");
	assert(utf8::to_PascalCase("startLine") == "StartLine");
	assert(utf8::to_PascalCase("StartLine") == "StartLine");
	assert(utf8::to_PascalCase("wait_ms_before_async") == "WaitMsBeforeAsync");
	assert(utf8::to_pascal_case("target_content") == "TargetContent");

	// to_camelCase
	assert(utf8::to_camelCase("") == "");
	assert(utf8::to_camelCase("path") == "path");
	assert(utf8::to_camelCase("start_line") == "startLine");
	assert(utf8::to_camelCase("StartLine") == "startLine");
	assert(utf8::to_camelCase("startLine") == "startLine");
	assert(utf8::to_camelCase("wait_ms_before_async") == "waitMsBeforeAsync");
	assert(utf8::to_camel_case("target_content") == "targetContent");

	// format_markdown_table_header
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | start_line | end_line | lines |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | StartLine | EndLine | Lines |\n");
	assert(utf8::format_markdown_table_header("| Symbol | path | start_line | end_line |\n") ==
	       "| Symbol | Path | StartLine | EndLine |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | Path | StartLine | EndLine |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | startLine | endLine | lines |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | path | startLine | endLine |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
}

void test_utf8_param_case_formatting()
{
	// 1. get_active_param_case_style / set_active_param_case_style
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
	assert(utf8::get_active_param_case_style() == utf8::param_case_style::snake_case);

	utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
	assert(utf8::get_active_param_case_style() == utf8::param_case_style::pascal_case);

	utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
	assert(utf8::get_active_param_case_style() == utf8::param_case_style::camel_case);

	// 2. format_param_name
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
	assert(utf8::format_param_name("start_line") == "start_line");
	assert(utf8::format_param_name("path") == "path");
	assert(utf8::format_param_name("target_content") == "target_content");
	assert(utf8::format_param_name("utf8_char") == "utf8_char");

	utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
	assert(utf8::format_param_name("start_line") == "StartLine");
	assert(utf8::format_param_name("path") == "Path");
	assert(utf8::format_param_name("target_content") == "TargetContent");
	assert(utf8::format_param_name("utf8_char") == "Utf8Char");

	utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
	assert(utf8::format_param_name("start_line") == "startLine");
	assert(utf8::format_param_name("path") == "path");
	assert(utf8::format_param_name("target_content") == "targetContent");
	assert(utf8::format_param_name("utf8_char") == "utf8Char");

	// 3. format_markdown_table_header
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | start_line | end_line | lines |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | StartLine | EndLine | Lines |\n");
	assert(utf8::format_markdown_table_header("| Symbol | path | start_line | end_line |\n") ==
	       "| Symbol | Path | StartLine | EndLine |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | Path | StartLine | EndLine |\n");
	// Header containing parameters with digits
	assert(utf8::format_markdown_table_header("| Method | arg1 | layer2_norm | count |\n") ==
	       "| Method | Arg1 | Layer2Norm | Count |\n");

	utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
	assert(utf8::format_markdown_table_header("| Symbol | start_line | end_line | lines |\n") ==
	       "| Symbol | startLine | endLine | lines |\n");
	assert(utf8::format_markdown_table_header("| Type | Kind | path | start_line | end_line |\n") ==
	       "| Type | Kind | path | startLine | endLine |\n");
	assert(utf8::format_markdown_table_header("| Method | arg1 | layer2_norm | count |\n") ==
	       "| Method | arg1 | layer2Norm | count |\n");

	// Reset to default snake_case
	utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
}

void test_utf8_case_idempotence()
{
	const std::vector<std::string_view> test_strings = {
		"",
		"a",
		"x",
		"word",
		"Word",
		"start_line",
		"StartLine",
		"startLine",
		"end_line",
		"EndLine",
		"endLine",
		"path",
		"Path",
		"file_path",
		"target_content",
		"TargetContent",
		"targetContent",
		"replacement_content",
		"ReplacementContent",
		"replacementContent",
		"wait_ms_before_async",
		"WaitMsBeforeAsync",
		"waitMsBeforeAsync",
		"is_daemon",
		"IsDaemon",
		"isDaemon",
		"already_snake_case",
		"camelCase",
		"PascalCase",
		"kebab-case-string",
		"SCREAMING_SNAKE_CASE",
		"with spaces in name",
		"mix-of_delimiters and_styles",
		"multi_word_long_identifier_name",
		"alpha_beta_gamma_delta",
		// Acronyms
		"URL",
		"url",
		"Url",
		"HTMLParser",
		"html_parser",
		"HtmlParser",
		"htmlParser",
		// Numbers
		"utf8_char",
		"utf8Char",
		"Utf8Char",
		"v2_engine",
		"v2Engine",
		"V2Engine",
		"run3_step",
		"run3Step",
		"Run3Step",
		"sha256_hash",
		"sha256Hash",
		"Sha256Hash",
		"base64_encode",
		"base64Encode",
		"Base64Encode",
		"mp3_player",
		"mp3Player",
		"Mp3Player",
		"iso9660_filesystem",
		"iso9660Filesystem",
		"Iso9660Filesystem",
		"plan9_protocol",
		"plan9Protocol",
		"Plan9Protocol",
		"h264_decoder",
		"h264Decoder",
		"H264Decoder",
		"model3_revision2",
		"model3Revision2",
		"Model3Revision2",
		"item1",
		"Item1",
		"test123",
		"Test123",
		"step1_alpha",
		"alpha2_beta3",
		"arg0",
		"param1",
		"layer2_norm",
		"win32_api",
		"ipv4_address",
		"ipv6_socket",
		"oauth2_token",
		"bfloat16_tensor",
		"fp32_precision",
		"int64_value",
		"uint32_count",
		"tls13_handshake",
		"http2_stream",
		"ec2_instance",
		"s3_bucket",
		"x86_64",
		"step_1",
		"item_2_prop_3",
		"level_99_boss",
		"p50_latency",
		"p99_latency",
		"v1_2_3",
		"port_8080",
		"width_1080p",
		"3d_model",
		"2_factor_auth",
		"100_percent",
		"4k_video",
		"pos0_x",
		"pos1_y",
		"pos2_z",
		"PascalCaseWithNumbers123",
		"camelCaseWithNumbers456",
		"UPPER_CASE_WITH_NUMBERS_789",
		// UTF-8
		"café_au_lait",
		"café_noir",
		"münchen_airport",
		"résumé_data",
		"déjà_vu",
		"cliché_story",
		"fiancé_status",
		"attaché_case",
		"naïve_bayes",
		"año_nuevo",
		"château_rouge",
		"über_file",
		"crème_brûlée_flavor",
		"piñata_hit",
		"jalapeño_pepper",
		"habanero_sauce",
		"façade_pattern",
		"coöperation_token",
		"grüße_aus_berlin",
		"smörgåsbord_table",
		"vis-à-vis_mode",
		"maître_d",
		"zaïre_currency",
		"côte_d_azur",
		"håkan_name",
		"são_paulo",
		"brötchen_bread",
		"tête_à_tête",
		"hello_world_螃蟹",
		"螃蟹_item",
		"漢字_name",
		"東京_tower",
		"русский_текст",
		"привет_мир",
		"москва_сити",
		"greek_Ω_omega",
		"math_π_pi",
		"alpha_β_gamma",
		"crab_🦀_test",
		"rocket_🚀_launch",
		"sparkles_✨_effect",
		"fire_🔥_event",
		// Delimiter edge cases
		"multiple__consecutive___underscores",
		"trailing_underscore_",
		"_leading_underscore",
		"__leading_and_trailing__",
		"mixed---delimiters___test",
		"kebab-and-snake_case-mix",
		"spaces  and   tabs",
	};

	for (std::string_view s : test_strings) {
		check_case_idempotence(s);
	}
}

static void verify_space_symmetry(std::string_view s)
{
	size_t n_chars = utf8::length(s);

	// Verify symmetry between is_space_at_char and is_space_at_byte for valid and out-of-range positions
	for (size_t char_pos = 0; char_pos <= n_chars + 5; ++char_pos) {
		size_t byte_offset = utf8::char_to_byte_offset(s, char_pos);
		bool space_char = utf8::is_space_at_char(s, char_pos);
		bool space_byte = utf8::is_space_at_byte(s, byte_offset);
		assert(space_char == space_byte);

		if (char_pos >= n_chars) {
			assert(!space_char);
			assert(!space_byte);
		}
	}

	// Verify all byte offsets directly
	for (size_t b = 0; b <= s.length() + 5; ++b) {
		bool space_byte = utf8::is_space_at_byte(s, b);
		if (b >= s.length()) {
			assert(!space_byte);
		} else {
			unsigned char uc = static_cast<unsigned char>(s[b]);
			// A UTF-8 continuation byte (0x80 - 0xBF) cannot be the start of a character/space
			if (uc >= 0x80 && uc < 0xC0) {
				assert(!space_byte);
			}
		}
	}
}

void test_utf8_is_space()
{
	// 1. Basic ASCII whitespace
	assert(utf8::is_space_at_char(" ", 0));
	assert(utf8::is_space_at_byte(" ", 0));
	assert(utf8::is_space_at_char("\t", 0));
	assert(utf8::is_space_at_byte("\t", 0));
	assert(utf8::is_space_at_char("\n", 0));
	assert(utf8::is_space_at_byte("\n", 0));
	assert(utf8::is_space_at_char("\r", 0));
	assert(utf8::is_space_at_byte("\r", 0));
	assert(utf8::is_space_at_char("\v", 0));
	assert(utf8::is_space_at_byte("\v", 0));
	assert(utf8::is_space_at_char("\f", 0));
	assert(utf8::is_space_at_byte("\f", 0));

	// ASCII non-whitespace
	assert(!utf8::is_space_at_char("a", 0));
	assert(!utf8::is_space_at_byte("a", 0));
	assert(!utf8::is_space_at_char("0", 0));
	assert(!utf8::is_space_at_byte("0", 0));
	assert(!utf8::is_space_at_char("_", 0));
	assert(!utf8::is_space_at_byte("_", 0));
	assert(!utf8::is_space_at_char("-", 0));
	assert(!utf8::is_space_at_byte("-", 0));

	// 2. Unicode spaces
	// U+00A0 (No-Break Space: \xC2\xA0)
	assert(utf8::is_space_at_char("\xC2\xA0", 0));
	assert(utf8::is_space_at_byte("\xC2\xA0", 0));
	assert(!utf8::is_space_at_byte("\xC2\xA0", 1)); // continuation byte

	// U+1680 (Ogham Space Mark: \xE1\x9A\x80)
	assert(utf8::is_space_at_char("\xE1\x9A\x80", 0));
	assert(utf8::is_space_at_byte("\xE1\x9A\x80", 0));
	assert(!utf8::is_space_at_byte("\xE1\x9A\x80", 1));
	assert(!utf8::is_space_at_byte("\xE1\x9A\x80", 2));

	// U+2000 through U+200A (En Quad, Em Quad, En Space, Em Space, Thin Space, Hair Space, etc.)
	for (unsigned char b2 = 0x80; b2 <= 0x8A; ++b2) {
		std::string space_str = {'\xE2', '\x80', static_cast<char>(b2)};
		assert(utf8::is_space_at_char(space_str, 0));
		assert(utf8::is_space_at_byte(space_str, 0));
		assert(!utf8::is_space_at_byte(space_str, 1));
		assert(!utf8::is_space_at_byte(space_str, 2));
	}

	// U+2028 (Line Separator: \xE2\x80\xA8)
	assert(utf8::is_space_at_char("\xE2\x80\xA8", 0));
	assert(utf8::is_space_at_byte("\xE2\x80\xA8", 0));

	// U+2029 (Paragraph Separator: \xE2\x80\xA9)
	assert(utf8::is_space_at_char("\xE2\x80\xA9", 0));
	assert(utf8::is_space_at_byte("\xE2\x80\xA9", 0));

	// U+202F (Narrow No-Break Space: \xE2\x80\xAF)
	assert(utf8::is_space_at_char("\xE2\x80\xAF", 0));
	assert(utf8::is_space_at_byte("\xE2\x80\xAF", 0));

	// U+205F (Medium Mathematical Space: \xE2\x81\x9F)
	assert(utf8::is_space_at_char("\xE2\x81\x9F", 0));
	assert(utf8::is_space_at_byte("\xE2\x81\x9F", 0));

	// U+3000 (Ideographic Space: \xE3\x80\x80)
	assert(utf8::is_space_at_char("\xE3\x80\x80", 0));
	assert(utf8::is_space_at_byte("\xE3\x80\x80", 0));
	assert(!utf8::is_space_at_byte("\xE3\x80\x80", 1));
	assert(!utf8::is_space_at_byte("\xE3\x80\x80", 2));

	// 3. Unicode non-spaces (adjacent or similar lead bytes to test against false positives)
	assert(!utf8::is_space_at_char("\xC2\x9F", 0));
	assert(!utf8::is_space_at_byte("\xC2\x9F", 0));
	assert(!utf8::is_space_at_char("\xC2\xA1", 0)); // Inverted exclamation ¡
	assert(!utf8::is_space_at_byte("\xC2\xA1", 0));
	assert(!utf8::is_space_at_char("\xE1\x9A\x81", 0)); // Ogham letter Beith (adjacent to U+1680)
	assert(!utf8::is_space_at_byte("\xE1\x9A\x81", 0));
	assert(!utf8::is_space_at_char("\xE1\x80\x80", 0)); // Myanmar letter Ka (different 2nd byte)
	assert(!utf8::is_space_at_byte("\xE1\x80\x80", 0));
	assert(!utf8::is_space_at_char("\xE2\x80\x8B", 0)); // Zero width space
	assert(!utf8::is_space_at_byte("\xE2\x80\x8B", 0));
	assert(!utf8::is_space_at_char("\xE2\x80\x90", 0)); // Hyphen
	assert(!utf8::is_space_at_byte("\xE2\x80\x90", 0));
	assert(!utf8::is_space_at_char("\xE2\x81\x9E", 0));
	assert(!utf8::is_space_at_byte("\xE2\x81\x9E", 0));
	assert(!utf8::is_space_at_char("\xE3\x80\x81", 0)); // Ideographic comma
	assert(!utf8::is_space_at_byte("\xE3\x80\x81", 0));
	assert(!utf8::is_space_at_char("中", 0));
	assert(!utf8::is_space_at_byte("中", 0));
	assert(!utf8::is_space_at_char("é", 0));
	assert(!utf8::is_space_at_byte("é", 0));
	assert(!utf8::is_space_at_char("🚀", 0));
	assert(!utf8::is_space_at_byte("🚀", 0));

	// 4. Boundary and error conditions
	// Empty string
	assert(!utf8::is_space_at_char("", 0));
	assert(!utf8::is_space_at_byte("", 0));
	assert(!utf8::is_space_at_char("", 10));
	assert(!utf8::is_space_at_byte("", 10));
	assert(!utf8::is_space_at_char("", static_cast<size_t>(-1)));
	assert(!utf8::is_space_at_byte("", static_cast<size_t>(-1)));

	// Truncated multi-byte space sequences (end-of-buffer safety)
	assert(!utf8::is_space_at_char("\xC2", 0));
	assert(!utf8::is_space_at_byte("\xC2", 0));
	assert(!utf8::is_space_at_char("\xE1\x9A", 0));
	assert(!utf8::is_space_at_byte("\xE1\x9A", 0));
	assert(!utf8::is_space_at_char("\xE2\x80", 0));
	assert(!utf8::is_space_at_byte("\xE2\x80", 0));
	assert(!utf8::is_space_at_char("\xE3\x80", 0));
	assert(!utf8::is_space_at_byte("\xE3\x80", 0));

	// Invalid UTF-8 lead and continuation bytes
	assert(!utf8::is_space_at_char("\xFF", 0));
	assert(!utf8::is_space_at_byte("\xFF", 0));
	assert(!utf8::is_space_at_char("\x80", 0));
	assert(!utf8::is_space_at_byte("\x80", 0));

	// 5. In-context string testing with symmetry verification
	const std::string nbsp = "\xC2\xA0";
	const std::string ogham = "\xE1\x9A\x80";
	const std::string quad = "\xE2\x80\x80";
	const std::string em_space = "\xE2\x80\x83";
	const std::string thin_space = "\xE2\x80\x89";
	const std::string hair_space = "\xE2\x80\x8A";
	const std::string cjk_space = "\xE3\x80\x80";

	std::string mixed1 = "Hello World";
	assert(!utf8::is_space_at_char(mixed1, 4));
	assert(utf8::is_space_at_char(mixed1, 5)); // ' '
	assert(!utf8::is_space_at_char(mixed1, 6));

	std::string mixed_nbsp = "Café" + nbsp + "Au" + nbsp + "Lait";
	// 'C'(0), 'a'(1), 'f'(2), 'é'(3), NBSP(4), 'A'(5), 'u'(6), NBSP(7), 'L'(8), 'a'(9), 'i'(10), 't'(11)
	assert(!utf8::is_space_at_char(mixed_nbsp, 3)); // 'é'
	assert(utf8::is_space_at_char(mixed_nbsp, 4));  // NBSP
	assert(!utf8::is_space_at_char(mixed_nbsp, 5)); // 'A'
	assert(utf8::is_space_at_char(mixed_nbsp, 7));  // NBSP
	assert(!utf8::is_space_at_char(mixed_nbsp, 8)); // 'L'

	std::string mixed_cjk = "こんにちは" + cjk_space + "世界";
	// 5 Japanese kana chars, then ideographic space at char 5, then 2 kanji chars
	assert(!utf8::is_space_at_char(mixed_cjk, 4));
	assert(utf8::is_space_at_char(mixed_cjk, 5));
	assert(!utf8::is_space_at_char(mixed_cjk, 6));

	// 6. Prove symmetry on an exhaustive suite of test strings
	const std::vector<std::string> symmetry_corpus = {
		"",
		" ",
		"   ",
		"\t\n\r",
		"a",
		"hello world",
		"  leading and trailing  ",
		"\tindented text\nnewline",
		nbsp,
		mixed_nbsp,
		cjk_space,
		mixed_cjk,
		ogham + "ogham" + ogham + "space",
		quad + "quad" + em_space + "em" + thin_space + "thin" + hair_space + "hair",
		"Mixed \t" + nbsp + ogham + em_space + cjk_space + "spaces",
		"🚀 sparkles ✨ fire 🔥",
		"x86_64 and arm64_v8a",
		"\xC2",
		"\xE2\x80",
		"\xE3\x80",
		std::string("invalid\xFF\x80") + "bytes"
	};

	for (const auto &str : symmetry_corpus) {
		verify_space_symmetry(str);
	}
}

void test_utf8_is_alnum()
{
	// 1. is_alnum(char, extra)
	// Digits and ASCII letters
	for (char c = '0'; c <= '9'; ++c) {
		assert(utf8::is_alnum(c));
		assert(utf8::is_alnum(c, "_-"));
	}
	for (char c = 'a'; c <= 'z'; ++c) {
		assert(utf8::is_alnum(c));
		assert(utf8::is_alnum(c, "_-"));
	}
	for (char c = 'A'; c <= 'Z'; ++c) {
		assert(utf8::is_alnum(c));
		assert(utf8::is_alnum(c, "_-"));
	}

	// Punctuation and whitespace
	assert(!utf8::is_alnum('_'));
	assert(utf8::is_alnum('_', "_"));
	assert(utf8::is_alnum('_', "-_"));
	assert(!utf8::is_alnum('-'));
	assert(utf8::is_alnum('-', "-_"));
	assert(utf8::is_alnum('/', "/."));
	assert(!utf8::is_alnum('/'));
	assert(!utf8::is_alnum(' '));
	assert(utf8::is_alnum(' ', " "));
	assert(!utf8::is_alnum('@'));
	assert(utf8::is_alnum('@', "@"));

	// Signedness and non-ASCII safety
	assert(!utf8::is_alnum(static_cast<char>(0x80)));
	assert(!utf8::is_alnum(static_cast<char>(0xFF)));
	assert(!utf8::is_alnum(static_cast<char>(-1)));
	assert(!utf8::is_alnum(static_cast<char>(-50)));

	// 2. is_alnum_at_byte
	std::string s1 = "var_name_123";
	assert(utf8::is_alnum_at_byte(s1, 0)); // 'v'
	assert(!utf8::is_alnum_at_byte(s1, 3)); // '_'
	assert(utf8::is_alnum_at_byte(s1, 3, "_")); // '_' allowed
	assert(utf8::is_alnum_at_byte(s1, 11)); // '3'
	assert(!utf8::is_alnum_at_byte(s1, 12)); // out of bounds
	assert(!utf8::is_alnum_at_byte("", 0));

	// Multi-byte characters at byte offset
	std::string s_cjk = "var_🦀_end";
	// 'v'(0), 'a'(1), 'r'(2), '_'(3), '🦀'(4..7), '_'(8), 'e'(9), 'n'(10), 'd'(11)
	assert(utf8::is_alnum_at_byte(s_cjk, 2)); // 'r'
	assert(!utf8::is_alnum_at_byte(s_cjk, 4)); // '🦀' without extra
	assert(utf8::is_alnum_at_byte(s_cjk, 4, "🦀")); // '🦀' with extra
	assert(!utf8::is_alnum_at_byte(s_cjk, 5, "🦀")); // continuation byte of 🦀
	assert(!utf8::is_alnum_at_byte(s_cjk, 6, "🦀"));
	assert(!utf8::is_alnum_at_byte(s_cjk, 7, "🦀"));

	// Truncated multi-byte sequence
	assert(!utf8::is_alnum_at_byte("\xC2", 0, "é"));

	// 3. is_alnum_at_char
	// In s_cjk: 'v'(0), 'a'(1), 'r'(2), '_'(3), '🦀'(4), '_'(5), 'e'(6), 'n'(7), 'd'(8)
	assert(utf8::is_alnum_at_char(s_cjk, 0)); // 'v'
	assert(!utf8::is_alnum_at_char(s_cjk, 3)); // '_'
	assert(utf8::is_alnum_at_char(s_cjk, 3, "_")); // '_'
	assert(!utf8::is_alnum_at_char(s_cjk, 4)); // '🦀'
	assert(utf8::is_alnum_at_char(s_cjk, 4, "🦀")); // '🦀'
	assert(utf8::is_alnum_at_char(s_cjk, 6)); // 'e'
	assert(!utf8::is_alnum_at_char(s_cjk, 10)); // out of bounds

	// 4. is_alnum_only
	assert(!utf8::is_alnum_only("")); // Empty string returns false
	assert(utf8::is_alnum_only("hello123WORLD"));
	assert(!utf8::is_alnum_only("hello_world"));
	assert(utf8::is_alnum_only("hello_world", "_"));
	assert(utf8::is_alnum_only("my-cool-db_01", "-_"));
	assert(!utf8::is_alnum_only("my cool db", "-_"));
	assert(!utf8::is_alnum_only("db.name", "-_"));
	assert(utf8::is_alnum_only("db.name", "-_."));
	assert(utf8::is_alnum_only("valid-slug_100", "-_"));

	// Multibyte support in is_alnum_only
	assert(!utf8::is_alnum_only("café"));
	assert(utf8::is_alnum_only("café", "é"));
	assert(!utf8::is_alnum_only("café\xC2", "é")); // Truncated sequence returns false

	// 5. is_not_alnum_only
	// Empty string contains no invalid characters -> false
	assert(!utf8::is_not_alnum_only(""));
	assert(!utf8::is_not_alnum_only("", "-_"));
	// Pure alphanumeric strings -> false
	assert(!utf8::is_not_alnum_only("hello123WORLD"));
	assert(!utf8::is_not_alnum_only("hello_world", "_"));
	assert(!utf8::is_not_alnum_only("test-123_abc", "-_"));
	// Strings containing invalid / disallowed characters -> true
	assert(utf8::is_not_alnum_only("hello_world")); // '_' not in extra
	assert(utf8::is_not_alnum_only("test 123", "-_"));
	assert(utf8::is_not_alnum_only("test;injection", "-_"));
	assert(utf8::is_not_alnum_only("db.name", "-_"));
	assert(!utf8::is_not_alnum_only("db.name", "-_."));
	// Multibyte support in is_not_alnum_only
	assert(utf8::is_not_alnum_only("café"));
	assert(!utf8::is_not_alnum_only("café", "é"));
	assert(utf8::is_not_alnum_only("café\xC2", "é"));
	// is_alnum_only with non-empty extra that does not match
	assert(!utf8::is_alnum_only("café", "xyz"));
}

void test_utf8_is_word_char()
{
	// 1. ASCII tests
	std::string_view ascii_code = "foo(bar_123);";
	// 'f', 'o', 'o'
	assert(utf8::is_word_char_at_byte(ascii_code, 0));
	assert(utf8::is_word_char_at_byte(ascii_code, 1));
	assert(utf8::is_word_char_at_byte(ascii_code, 2));
	// '(' delimiter
	assert(!utf8::is_word_char_at_byte(ascii_code, 3));
	// 'b', 'a', 'r', '_', '1', '2', '3'
	assert(utf8::is_word_char_at_byte(ascii_code, 4));
	assert(utf8::is_word_char_at_byte(ascii_code, 7)); // '_'
	assert(utf8::is_word_char_at_byte(ascii_code, 10)); // '3'
	// ')' and ';' delimiters
	assert(!utf8::is_word_char_at_byte(ascii_code, 11));
	assert(!utf8::is_word_char_at_byte(ascii_code, 12));
	assert(!utf8::is_word_char_at_byte(ascii_code, 100)); // Out of bounds

	// 2. Multi-byte Unicode word characters (accents, Greek, German, CJK)
	std::string_view unicode_words = "café π_val groß 日本語";
	// 'é' in café (byte offset 3)
	assert(utf8::is_word_char_at_byte(unicode_words, 3));
	// Space at byte offset 5
	assert(!utf8::is_word_char_at_byte(unicode_words, 5));
	// 'π' (byte offset 6)
	assert(utf8::is_word_char_at_byte(unicode_words, 6));
	// 'ß' in groß (byte offset 16)
	assert(utf8::is_word_char_at_byte(unicode_words, 16));
	// '日' (byte offset 19), '本' (byte offset 22), '語' (byte offset 25)
	assert(utf8::is_word_char_at_byte(unicode_words, 19));
	assert(utf8::is_word_char_at_byte(unicode_words, 22));
	assert(utf8::is_word_char_at_byte(unicode_words, 25));

	// 3. Unicode whitespace and delimiters
	// NBSP (U+00A0: \xC2\xA0)
	std::string_view nbsp_str = "a\xC2\xA0""b";
	assert(!utf8::is_word_char_at_byte(nbsp_str, 1));
	// Ideographic fullwidth space (U+3000: \xE3\x80\x80)
	std::string_view cjk_space = "a\xE3\x80\x80""b";
	assert(!utf8::is_word_char_at_byte(cjk_space, 1));
	// Guillemets (U+00AB: \xC2\xAB, U+00BB: \xC2\xBB)
	std::string_view guillemets = "\xC2\xABword\xC2\xBB";
	assert(!utf8::is_word_char_at_byte(guillemets, 0));
	assert(utf8::is_word_char_at_byte(guillemets, 2)); // 'w'
	assert(!utf8::is_word_char_at_byte(guillemets, 6)); // '»'
	// Smart quotes (U+201C: \xE2\x80\x9C, U+201D: \xE2\x80\x9D)
	std::string_view smart_quotes = "\xE2\x80\x9Cword\xE2\x80\x9D";
	assert(!utf8::is_word_char_at_byte(smart_quotes, 0));
	assert(utf8::is_word_char_at_byte(smart_quotes, 3)); // 'w'
	assert(!utf8::is_word_char_at_byte(smart_quotes, 7)); // close quote
	// Em-dash (U+2014: \xE2\x80\x94)
	std::string_view em_dash = "word\xE2\x80\x94word";
	assert(!utf8::is_word_char_at_byte(em_dash, 4));
	// CJK ideographic comma (U+3001: \xE3\x80\x81) and full stop (U+3002: \xE3\x80\x82)
	std::string_view cjk_punct = "文\xE3\x80\x81章\xE3\x80\x82";
	assert(utf8::is_word_char_at_byte(cjk_punct, 0)); // '文'
	assert(!utf8::is_word_char_at_byte(cjk_punct, 3)); // '、'
	assert(utf8::is_word_char_at_byte(cjk_punct, 6)); // '章'
	assert(!utf8::is_word_char_at_byte(cjk_punct, 9)); // '。'

	// Fullwidth ASCII punctuation variants
	std::string_view fw_punct = "（［｛｀～！：";
	for (size_t offset = 0; offset < fw_punct.length(); offset += 3) {
		assert(!utf8::is_word_char_at_byte(fw_punct, offset));
	}
	// Fullwidth low line / underscore U+FF3F is a valid identifier character
	std::string_view fw_underscore = "＿id";
	assert(utf8::is_word_char_at_byte(fw_underscore, 0));

	// Truncated multi-byte sequences and raw continuation bytes
	std::string_view trunc_seq = "\xC2";
	assert(!utf8::is_word_char_at_byte(trunc_seq, 0));
	std::string_view cont_byte = "\x80";
	assert(!utf8::is_word_char_at_byte(cont_byte, 0));

	// 4. Character position wrapper is_word_char_at_char
	std::string_view mixed = "// 日本語 café()";
	// 0: '/', 1: '/', 2: ' ', 3: '日', 4: '本', 5: '語', 6: ' ', 7: 'c', 8: 'a', 9: 'f', 10: 'é', 11: '(', 12: ')'
	assert(utf8::is_word_char_at_char(mixed, 3)); // '日'
	assert(utf8::is_word_char_at_char(mixed, 10)); // 'é'
	assert(!utf8::is_word_char_at_char(mixed, 6)); // ' '
	assert(!utf8::is_word_char_at_char(mixed, 11)); // '('
}

void test_utf8_is_delimiter()
{
	// 1. ASCII delimiters
	std::string_view ascii_code = "foo(bar_123, \"hello\"); +-*/=;:<>?[]{}";
	assert(!utf8::is_delimiter_at_byte(ascii_code, 0)); // 'f'
	assert(!utf8::is_delimiter_at_byte(ascii_code, 1)); // 'o'
	assert(utf8::is_delimiter_at_byte(ascii_code, 3));  // '('
	assert(!utf8::is_delimiter_at_byte(ascii_code, 4)); // 'b'
	assert(!utf8::is_delimiter_at_byte(ascii_code, 7)); // '_' (extra)
	assert(!utf8::is_delimiter_at_byte(ascii_code, 10)); // '3'
	assert(utf8::is_delimiter_at_byte(ascii_code, 11)); // ','
	assert(!utf8::is_delimiter_at_byte(ascii_code, 12)); // ' ' (whitespace)
	assert(utf8::is_delimiter_at_byte(ascii_code, 13)); // '"'
	assert(utf8::is_delimiter_at_byte(ascii_code, 19)); // '"'
	assert(utf8::is_delimiter_at_byte(ascii_code, 20)); // ')'
	assert(utf8::is_delimiter_at_byte(ascii_code, 21)); // ';'
	assert(!utf8::is_delimiter_at_byte(ascii_code, 22)); // ' '
	assert(utf8::is_delimiter_at_byte(ascii_code, 23)); // '+'
	assert(utf8::is_delimiter_at_byte(ascii_code, 24)); // '-'

	// Control chars and out of bounds
	assert(!utf8::is_delimiter_at_byte("", 0));
	assert(!utf8::is_delimiter_at_byte(ascii_code, 100));
	std::string_view null_str("\0foo", 4);
	assert(!utf8::is_delimiter_at_byte(null_str, 0)); // control char < 0x20

	// 2. Unicode delimiters and punctuation
	std::string_view unicode_delims = "«word» “quote” 文、章。 （全角） —dash";
	// '«' (offset 0, 2 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 0));
	assert(!utf8::is_delimiter_at_byte(unicode_delims, 2)); // 'w'
	// '»' (offset 6, 2 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 6));
	assert(!utf8::is_delimiter_at_byte(unicode_delims, 8)); // ' '
	// '“' (offset 9, 3 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 9));
	// '”' (offset 17, 3 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 17));
	// CJK '、' (offset 24, 3 bytes) and '。' (offset 30, 3 bytes)
	assert(!utf8::is_delimiter_at_byte(unicode_delims, 21)); // '文'
	assert(utf8::is_delimiter_at_byte(unicode_delims, 24));  // '、'
	assert(!utf8::is_delimiter_at_byte(unicode_delims, 27)); // '章'
	assert(utf8::is_delimiter_at_byte(unicode_delims, 30));  // '。'
	// Fullwidth parens '（' (offset 34, 3 bytes) and '）' (offset 43, 3 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 34)); // '（'
	assert(!utf8::is_delimiter_at_byte(unicode_delims, 37)); // '全'
	assert(utf8::is_delimiter_at_byte(unicode_delims, 43)); // '）'
	// Em dash '—' (offset 47, 3 bytes)
	assert(utf8::is_delimiter_at_byte(unicode_delims, 47)); // '—'

	// Unicode whitespace and word chars are NOT delimiters
	std::string_view nbsp_str = "a\xC2\xA0""b";
	assert(!utf8::is_delimiter_at_byte(nbsp_str, 1)); // NBSP
	std::string_view fw_underscore = "＿id";
	assert(!utf8::is_delimiter_at_byte(fw_underscore, 0)); // '＿'

	// Truncated / continuation bytes
	assert(!utf8::is_delimiter_at_byte("\xC2", 0));
	assert(!utf8::is_delimiter_at_byte("\x80", 0));

	// 3. Character position wrappers and terminator aliases
	std::string_view mixed = "// 日本語 café()";
	// 0: '/', 1: '/', 2: ' ', 3: '日', 4: '本', 5: '語', 6: ' ', 7: 'c', 8: 'a', 9: 'f', 10: 'é', 11: '(', 12: ')'
	assert(utf8::is_delimiter_at_char(mixed, 0)); // '/'
	assert(utf8::is_delimiter_at_char(mixed, 1)); // '/'
	assert(!utf8::is_delimiter_at_char(mixed, 2)); // ' '
	assert(!utf8::is_delimiter_at_char(mixed, 3)); // '日'
	assert(!utf8::is_delimiter_at_char(mixed, 10)); // 'é'
	assert(utf8::is_delimiter_at_char(mixed, 11)); // '('
	assert(utf8::is_delimiter_at_char(mixed, 12)); // ')'

	// Semantic alias is_terminator_at_*
	assert(utf8::is_terminator_at_byte(mixed, 0));
	assert(!utf8::is_terminator_at_char(mixed, 3));
	assert(utf8::is_terminator_at_char(mixed, 11));
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
	test_utf8_sanitize_and_validation();
	test_utf8_trim_and_terminal_sanitize();
	test_utf8_case_conversions();
	test_utf8_param_case_formatting();
	test_utf8_case_idempotence();
	test_utf8_is_space();
	test_utf8_is_alnum();
	test_utf8_is_word_char();
	test_utf8_is_delimiter();

	std::cout << "All UTF-8 unit tests passed!" << std::endl;
	return 0;
}

