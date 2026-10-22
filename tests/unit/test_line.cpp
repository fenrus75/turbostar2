// Tested source file: src/line.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <atomic>
#include <thread>
#include "line.h"

int main()
{
	std::setlocale(LC_ALL, "");
	test_watchdog::setup_watchdog(30);
	// Japanese 'あ' is 3 bytes: \xe3\x81\x82
	// Emoji '😊' is 4 bytes: \xf0\x9f\x98\x8a
	line l("aあ😊b");

	// Character indices:
	// 0: 'a' (1 byte)
	// 1: 'あ' (3 bytes)
	// 2: '😊' (4 bytes)
	// 3: 'b' (1 byte)
	assert(l.length_in_chars() == 4);
	assert(l.length_in_bytes() == 9);

	// Test append_to
	std::string appended = "prefix: ";
	l.append_to(appended);
	assert(appended == "prefix: aあ😊b");

	// Test get_word_span_at and get_word_at with delimiters and identifiers
	{
		line l_code("foo(something) + bar_123;");
		// 'foo' at char 0..3
		auto span0 = l_code.get_word_span_at(0);
		assert(!span0.empty());
		assert(span0.word == "foo");
		assert(span0.start_char_pos == 0);
		assert(span0.end_char_pos == 3);
		assert(l_code.get_word_at(1) == "foo");
		assert(l_code.get_word_at(2) == "foo");

		// '(' delimiter at char 3 -> empty
		assert(l_code.get_word_span_at(3).empty());
		assert(l_code.get_word_at(3).empty());

		// 'something' at char 4..13
		auto span_some = l_code.get_word_span_at(4);
		assert(!span_some.empty());
		assert(span_some.word == "something");
		assert(span_some.start_char_pos == 4);
		assert(span_some.end_char_pos == 13);
		assert(l_code.get_word_at(12) == "something");

		// ')' delimiter at char 13 -> empty
		assert(l_code.get_word_span_at(13).empty());

		// 'bar_123' at char 17..24
		auto span_bar = l_code.get_word_span_at(17);
		assert(!span_bar.empty());
		assert(span_bar.word == "bar_123");
		assert(span_bar.start_char_pos == 17);
		assert(span_bar.end_char_pos == 24);
	}

	// Test get_word_span_at with multi-byte UTF-8 prefixes
	{
		line l_utf8("// 日本語 foo_bar();");
		// 0: '/', 1: '/', 2: ' ', 3: '日', 4: '本', 5: '語', 6: ' ', 7: 'f'..14: '('
		auto span_utf8 = l_utf8.get_word_span_at(7);
		assert(!span_utf8.empty());
		assert(span_utf8.word == "foo_bar");
		assert(span_utf8.start_char_pos == 7);
		assert(span_utf8.end_char_pos == 14);
		assert(l_utf8.get_word_at(10) == "foo_bar");
		assert(l_utf8.get_word_span_at(14).empty()); // '('
	}

	// Test cursor at end of line (EOL)
	{
		line l_eol("test_eol");
		auto span_eol = l_eol.get_word_span_at(8);
		assert(span_eol.word == "test_eol");
		assert(span_eol.start_char_pos == 0);
		assert(span_eol.end_char_pos == 8);

		line l_semicolon("test_eol;");
		assert(l_semicolon.get_word_span_at(9).empty());
	}

	// Test get_word_span_at with multi-byte characters in the middle of words
	{
		line l_accent("café_latte(x) + π_val * groß_handler;");
		// 'café_latte': 'c'(0), 'a'(1), 'f'(2), 'é'(3), '_'(4), 'l'(5)... 'e'(9)
		auto span_cafe = l_accent.get_word_span_at(3); // on 'é'
		assert(!span_cafe.empty());
		assert(span_cafe.word == "café_latte");
		assert(span_cafe.start_char_pos == 0);
		assert(span_cafe.end_char_pos == 10);
		assert(l_accent.get_word_at(0) == "café_latte");
		assert(l_accent.get_word_at(5) == "café_latte");

		// 'π_val': 'π'(16), '_'(17), 'v'(18), 'a'(19), 'l'(20)
		auto span_pi = l_accent.get_word_span_at(16); // on 'π'
		assert(span_pi.word == "π_val");
		assert(span_pi.start_char_pos == 16);
		assert(span_pi.end_char_pos == 21);

		// 'groß_handler': 'g'(24)... 'ß'(27)...
		auto span_gross = l_accent.get_word_span_at(27); // on 'ß'
		assert(span_gross.word == "groß_handler");
		assert(span_gross.start_char_pos == 24);
		assert(span_gross.end_char_pos == 36);
	}

	// Display column calculations
	// Japanese 'あ' is 2 columns, Emoji '😊' is 2 columns.
	// So: 'a' at col 0 (width 1), 'あ' at col 1 (width 2), '😊' at col 3 (width 2), 'b' at col 5, end at col 6
	assert(l.char_to_display_col(0) == 0);
	assert(l.char_to_display_col(1) == 1);
	assert(l.char_to_display_col(2) == 3);
	assert(l.char_to_display_col(3) == 5);
	assert(l.char_to_display_col(4) == 6);

	// Test bounds check in remove_at
	l.remove_at(10); // Out of bounds, should do nothing
	assert(l.length_in_chars() == 4);
	assert(l.get_text() == "aあ😊b");

	l.remove_at(-1); // Invalid index, should do nothing
	assert(l.length_in_chars() == 4);

	// Test bounds check/clamping in insert_at
	l.insert_at(10, "x"); // Should clamp to end and append
	assert(l.length_in_chars() == 5);
	assert(l.get_text() == "aあ😊bx");

	// Test display_col_to_char_pos on the updated line
	// 'a' (0 -> col 0), 'あ' (1 -> col 1), '😊' (2 -> col 3), 'b' (3 -> col 5), 'x' (4 -> col 6), end at col 7
	assert(l.display_col_to_char_pos(0) == 0);
	assert(l.display_col_to_char_pos(1) == 1);
	assert(l.display_col_to_char_pos(2) == 1); // 2 is closer to 1 (diff 1) than 3 (diff 1)
	assert(l.display_col_to_char_pos(3) == 2);
	assert(l.display_col_to_char_pos(4) == 2); // 4 is closer to 3 (diff 1) than 5 (diff 1)
	assert(l.display_col_to_char_pos(5) == 3);
	assert(l.display_col_to_char_pos(6) == 4);
	assert(l.display_col_to_char_pos(7) == 5);
	assert(l.display_col_to_char_pos(10) == 5);
	assert(l.display_col_to_char_pos(-1) == 0);

	// Test display_col_to_char_pos with tabs
	line l_tab("a\tb");
	// 'a' (0 -> col 0), '\t' (1 -> col 1, tab size up to 8 cells, so 'b' will be at col 8)
	// tab width at col 1 is 8 - (1 % 8) = 7 cells, so 'b' is at display col 8
	assert(l_tab.char_to_display_col(0) == 0);
	assert(l_tab.char_to_display_col(1) == 1);
	assert(l_tab.char_to_display_col(2) == 8);
	assert(l_tab.char_to_display_col(3) == 9);

	// display_col_to_char_pos should map closest display columns:
	assert(l_tab.display_col_to_char_pos(0) == 0);
	assert(l_tab.display_col_to_char_pos(1) == 1);
	assert(l_tab.display_col_to_char_pos(2) == 1); // 2 is closer to 1 than to 8
	assert(l_tab.display_col_to_char_pos(4) == 1); // 4 is closer to 1 (diff 3) than 8 (diff 4)
	assert(l_tab.display_col_to_char_pos(5) == 2); // 5 is closer to 8 (diff 3) than 1 (diff 4)
	assert(l_tab.display_col_to_char_pos(8) == 2);
	assert(l_tab.display_col_to_char_pos(9) == 3);

	// Test line::is_space_at, is_terminator_at, is_delimiter_at
	{
		// 0: 'f', 1: 'o', 2: 'o', 3: '(', 4: 'a', 5: 'b', 6: 'c', 7: ')', 8: ' ',
		// 9: '«', 10: 'm', 11: 'o', 12: 't', 13: '»', 14: ';', 15: ' ', 16: '関', 17: '数',
		// 18: '（', 19: '引', 20: '数', 21: '）'
		line l_delims("foo(abc) «mot»; 関数（引数）");

		// Out of bounds and negative checks
		assert(!l_delims.is_space_at(-1));
		assert(!l_delims.is_space_at(100));
		assert(!l_delims.is_terminator_at(-1));
		assert(!l_delims.is_terminator_at(100));
		assert(!l_delims.is_delimiter_at(-1));
		assert(!l_delims.is_delimiter_at(100));

		// Word characters: neither space nor delimiter
		assert(!l_delims.is_space_at(0));
		assert(!l_delims.is_delimiter_at(0));
		assert(!l_delims.is_terminator_at(0));
		assert(!l_delims.is_space_at(16)); // '関'
		assert(!l_delims.is_delimiter_at(16));
		assert(!l_delims.is_terminator_at(16));

		// ASCII delimiters: '(' at 3, ')' at 7, ';' at 14
		assert(l_delims.is_delimiter_at(3));
		assert(l_delims.is_terminator_at(3));
		assert(!l_delims.is_space_at(3));
		assert(l_delims.is_delimiter_at(7));
		assert(l_delims.is_terminator_at(7));
		assert(l_delims.is_delimiter_at(14));
		assert(l_delims.is_terminator_at(14));

		// Spaces: ' ' at 8 and 15
		assert(l_delims.is_space_at(8));
		assert(!l_delims.is_delimiter_at(8));
		assert(!l_delims.is_terminator_at(8));
		assert(l_delims.is_space_at(15));
		assert(!l_delims.is_delimiter_at(15));

		// Unicode delimiters: '«' at 9, '»' at 13, '（' at 18, '）' at 21
		assert(l_delims.is_delimiter_at(9));
		assert(l_delims.is_terminator_at(9));
		assert(l_delims.is_delimiter_at(13));
		assert(l_delims.is_terminator_at(13));
		assert(l_delims.is_delimiter_at(18));
		assert(l_delims.is_terminator_at(18));
		assert(l_delims.is_delimiter_at(21));
		assert(l_delims.is_terminator_at(21));
	}

	// Test get_whitespace_span_at
	{
		line l_empty("");
		assert(l_empty.get_whitespace_span_at(0).empty());
		assert(l_empty.get_whitespace_span_at(-1).empty());

		// "  foo\t\tbar 　 done "
		// 0..1: "  " (spaces)
		// 2..4: "foo"
		// 5..6: "\t\t" (tabs)
		// 7..9: "bar"
		// 10: ' ' (space)
		// 11: '　' (U+3000 ideographic space)
		// 12: ' ' (space)
		// 13..16: "done"
		// 17: ' ' (space)
		line l_ws("  foo\t\tbar 　 done ");

		// Out of bounds
		assert(l_ws.get_whitespace_span_at(-1).empty());
		assert(l_ws.get_whitespace_span_at(18).empty());

		// On non-whitespace:
		assert(l_ws.get_whitespace_span_at(2).empty()); // 'f'
		assert(l_ws.get_whitespace_span_at(7).empty()); // 'b'
		assert(l_ws.get_whitespace_span_at(13).empty()); // 'd'

		// Leading spaces [0, 2)
		auto ws_lead0 = l_ws.get_whitespace_span_at(0);
		assert(!ws_lead0.empty());
		assert(ws_lead0.word == "  ");
		assert(ws_lead0.start_char_pos == 0);
		assert(ws_lead0.end_char_pos == 2);

		auto ws_lead1 = l_ws.get_whitespace_span_at(1);
		assert(ws_lead1.word == "  ");
		assert(ws_lead1.start_char_pos == 0);
		assert(ws_lead1.end_char_pos == 2);

		// Middle tabs [5, 7)
		auto ws_tabs = l_ws.get_whitespace_span_at(5);
		assert(!ws_tabs.empty());
		assert(ws_tabs.word == "\t\t");
		assert(ws_tabs.start_char_pos == 5);
		assert(ws_tabs.end_char_pos == 7);

		// Mixed ASCII + U+3000 Unicode space [10, 13)
		auto ws_mixed = l_ws.get_whitespace_span_at(11); // on '　'
		assert(!ws_mixed.empty());
		assert(ws_mixed.word == " 　 ");
		assert(ws_mixed.start_char_pos == 10);
		assert(ws_mixed.end_char_pos == 13);

		// Trailing space [17, 18)
		auto ws_trail = l_ws.get_whitespace_span_at(17);
		assert(!ws_trail.empty());
		assert(ws_trail.word == " ");
		assert(ws_trail.start_char_pos == 17);
		assert(ws_trail.end_char_pos == 18);
	}

	// Test copy and move semantics (including self-assignment)
	{
		line orig("hello world");
		orig.set_attributes({syntax_attribute::keyword, syntax_attribute::normal});

		// Copy construction
		line copy_constructed(orig);
		assert(copy_constructed.get_text() == "hello world");
		assert(copy_constructed.get_attribute(0) == syntax_attribute::keyword);

		// Copy assignment
		line copy_assigned("placeholder");
		copy_assigned = orig;
		assert(copy_assigned.get_text() == "hello world");
		assert(copy_assigned.get_attribute(0) == syntax_attribute::keyword);

		// Self copy assignment
		orig = *&orig;
		assert(orig.get_text() == "hello world");

		// Move construction
		line move_src("move text");
		move_src.set_attributes({syntax_attribute::string_literal});
		line move_constructed(std::move(move_src));
		assert(move_constructed.get_text() == "move text");
		assert(move_constructed.get_attribute(0) == syntax_attribute::string_literal);

		// Move assignment
		line move_assigned("old text");
		line move_source2("new moved text");
		move_source2.set_attributes({syntax_attribute::comment});
		move_assigned = std::move(move_source2);
		assert(move_assigned.get_text() == "new moved text");
		assert(move_assigned.get_attribute(0) == syntax_attribute::comment);

		// Self move assignment
		line *orig_ptr = &orig;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
		orig = std::move(*orig_ptr);
#pragma GCC diagnostic pop
		assert(orig.get_text() == "hello world");
	}

	// Test get_content and set_text
	{
		line l_content("sample code");
		l_content.set_attributes({syntax_attribute::keyword, syntax_attribute::keyword});
		std::string out_text;
		std::vector<syntax_attribute> out_attrs;
		l_content.get_content(out_text, out_attrs);
		assert(out_text == "sample code");
		assert(out_attrs.size() >= 2);
		assert(out_attrs[0] == syntax_attribute::keyword);

		l_content.set_text("replaced text");
		assert(l_content.get_text() == "replaced text");
		assert(l_content.length_in_chars() == 13);
	}

	// Test next_utf8_character
	{
		line l_utf("aあ😊");
		size_t offset = 0;
		std::string ch;
		assert(l_utf.next_utf8_character(offset, ch));
		assert(ch == "a");
		assert(offset == 1);

		assert(l_utf.next_utf8_character(offset, ch));
		assert(ch == "あ");
		assert(offset == 4);

		assert(l_utf.next_utf8_character(offset, ch));
		assert(ch == "😊");
		assert(offset == 8);

		assert(!l_utf.next_utf8_character(offset, ch));
	}

	// Test insert_at and split_at boundary cases
	{
		line l_ins("middle");
		l_ins.insert_at(-5, "start_");
		assert(l_ins.get_text() == "start_middle");

		line l_split("abcdef");
		line l_part1("");
		l_split.split_at(-1, l_part1);
		assert(l_split.get_text() == "");
		assert(l_part1.get_text() == "abcdef");

		line l_split2("hello");
		line l_part2("");
		l_split2.split_at(100, l_part2);
		assert(l_split2.get_text() == "hello");
		assert(l_part2.get_text() == "");
	}

	// Test byte_at, get_attribute bounds
	{
		line l_byte("abc");
		assert(l_byte.byte_at(0) == 'a');
		assert(l_byte.byte_at(1) == 'b');
		assert(l_byte.byte_at(2) == 'c');
		assert(l_byte.byte_at(-1) == 0);
		assert(l_byte.byte_at(3) == 0);
		assert(l_byte.byte_at(10) == 0);

		l_byte.set_attributes({syntax_attribute::keyword, syntax_attribute::normal});
		assert(l_byte.get_attribute(0) == syntax_attribute::keyword);
		assert(l_byte.get_attribute(1) == syntax_attribute::normal);
		assert(l_byte.get_attribute(-1) == syntax_attribute::normal);
		assert(l_byte.get_attribute(5) == syntax_attribute::normal);
	}

	// Test display column calculation with truncated UTF-8 at EOL
	{
		// 0xC3 starts a 2-byte UTF-8 sequence, but has no continuation byte
		std::string truncated_str = "ab\xC3";
		line l_trunc(truncated_str);
		int col = l_trunc.char_to_display_col(3);
		assert(col >= 2);
	}

	// Test get_whitespace_span_at backward multi-byte continuation byte step
	{
		// Two consecutive U+3000 ideographic spaces: "\xE3\x80\x80\xE3\x80\x80"
		line l_two_cjk("a　　b");
		// 0: 'a'
		// 1: '　'
		// 2: '　'
		// 3: 'b'
		auto span_cjk2 = l_two_cjk.get_whitespace_span_at(2);
		assert(!span_cjk2.empty());
		assert(span_cjk2.start_char_pos == 1);
		assert(span_cjk2.end_char_pos == 3);
		assert(span_cjk2.word == "　　");
	}

	// Test get_delimiter_span_at and get_delimiter_at
	{
		line l_delims("foo(bar) == baz «test» ;;;");
		// 0..2: "foo"
		// 3: '('
		// 4..6: "bar"
		// 7: ')'
		// 8: ' '
		// 9..10: "=="
		// 11: ' '
		// 12..14: "baz"
		// 15: ' '
		// 16: '«'
		// 17..20: "test"
		// 21: '»'
		// 22: ' '
		// 23..25: ";;;"

		// On word char -> empty
		assert(l_delims.get_delimiter_span_at(0).empty());
		assert(l_delims.get_delimiter_at(0).empty());

		// On whitespace -> empty
		assert(l_delims.get_delimiter_span_at(8).empty());
		assert(l_delims.get_delimiter_at(8).empty());

		// On '(' single delimiter
		auto span_paren = l_delims.get_delimiter_span_at(3);
		assert(!span_paren.empty());
		assert(span_paren.word == "(");
		assert(span_paren.start_char_pos == 3);
		assert(span_paren.end_char_pos == 4);
		assert(l_delims.get_delimiter_at(3) == "(");

		// On '==' contiguous delimiters
		auto span_eq = l_delims.get_delimiter_span_at(9);
		assert(!span_eq.empty());
		assert(span_eq.word == "==");
		assert(span_eq.start_char_pos == 9);
		assert(span_eq.end_char_pos == 11);
		assert(l_delims.get_delimiter_at(10) == "==");

		// On '«' Unicode delimiter
		auto span_guillemet = l_delims.get_delimiter_span_at(16);
		assert(!span_guillemet.empty());
		assert(span_guillemet.word == "«");
		assert(span_guillemet.start_char_pos == 16);
		assert(span_guillemet.end_char_pos == 17);

		// On ';;;' at end of line
		auto span_semi = l_delims.get_delimiter_span_at(24);
		assert(!span_semi.empty());
		assert(span_semi.word == ";;;");
		assert(span_semi.start_char_pos == 23);
		assert(span_semi.end_char_pos == 26);

		// Parked at EOL (char_pos == total_chars) tests preceding char
		auto span_eol = l_delims.get_delimiter_span_at(26);
		assert(!span_eol.empty());
		assert(span_eol.word == ";;;");
		assert(span_eol.start_char_pos == 23);
		assert(span_eol.end_char_pos == 26);

		// Out of bounds / negative
		assert(l_delims.get_delimiter_span_at(-1).empty());
		assert(l_delims.get_delimiter_span_at(100).empty());

		// Empty line
		line l_empty("");
		assert(l_empty.get_delimiter_span_at(0).empty());
	}

	// Test get_element_span_at and get_element_at with span_kind
	{
		line l_elem("hello   world ===");
		// 0..4: "hello" (word)
		// 5..7: "   " (whitespace)
		// 8..12: "world" (word)
		// 13: ' ' (whitespace)
		// 14..16: "===" (delimiter)

		// 1. On word "hello"
		auto sp_word = l_elem.get_element_span_at(2);
		assert(!sp_word.empty());
		assert(sp_word.word == "hello");
		assert(sp_word.start_char_pos == 0);
		assert(sp_word.end_char_pos == 5);
		assert(sp_word.kind == span_kind::word);
		assert(l_elem.get_element_at(2) == "hello");

		// 2. On whitespace "   "
		auto sp_ws = l_elem.get_element_span_at(6);
		assert(!sp_ws.empty());
		assert(sp_ws.word == "   ");
		assert(sp_ws.start_char_pos == 5);
		assert(sp_ws.end_char_pos == 8);
		assert(sp_ws.kind == span_kind::whitespace);
		assert(l_elem.get_element_at(6) == "   ");

		// 3. On delimiter "==="
		auto sp_delim = l_elem.get_element_span_at(15);
		assert(!sp_delim.empty());
		assert(sp_delim.word == "===");
		assert(sp_delim.start_char_pos == 14);
		assert(sp_delim.end_char_pos == 17);
		assert(sp_delim.kind == span_kind::delimiter);
		assert(l_elem.get_element_at(15) == "===");

		// 4. At EOL
		auto sp_eol = l_elem.get_element_span_at(17);
		assert(!sp_eol.empty());
		assert(sp_eol.word == "===");
		assert(sp_eol.start_char_pos == 14);
		assert(sp_eol.end_char_pos == 17);
		assert(sp_eol.kind == span_kind::delimiter);

		// 5. Out of bounds
		assert(l_elem.get_element_span_at(-1).empty());
		assert(l_elem.get_element_span_at(100).empty());
		assert(l_elem.get_element_span_at(-1).kind == span_kind::none);

		// 6. Empty line
		line l_empty("");
		assert(l_empty.get_element_span_at(0).empty());
		assert(l_empty.get_element_span_at(0).kind == span_kind::none);

		// 7. Scope delimiter expansion (Option 2 compound scoping)
		line l_scoped("std::string");
		// On "std"
		auto sp_scope1 = l_scoped.get_element_span_at(1, "_", "::");
		assert(sp_scope1.word == "std::string");
		assert(sp_scope1.start_char_pos == 0);
		assert(sp_scope1.end_char_pos == 11);
		assert(sp_scope1.kind == span_kind::word);
		assert(l_scoped.get_element_at(1, "_", "::") == "std::string");

		// On "::"
		auto sp_scope2 = l_scoped.get_element_span_at(3, "_", "::");
		assert(sp_scope2.word == "std::string");
		assert(sp_scope2.start_char_pos == 0);
		assert(sp_scope2.end_char_pos == 11);

		// On "string"
		auto sp_scope3 = l_scoped.get_element_span_at(8, "_", "::");
		assert(sp_scope3.word == "std::string");
		assert(sp_scope3.start_char_pos == 0);
		assert(sp_scope3.end_char_pos == 11);

		// 8. Multi-level scoping: "a::b::c::d"
		line l_multi("a::b::c::d");
		assert(l_multi.get_element_at(0, "_", "::") == "a::b::c::d");
		assert(l_multi.get_element_at(3, "_", "::") == "a::b::c::d");
		assert(l_multi.get_element_at(5, "_", "::") == "a::b::c::d");
		assert(l_multi.get_element_at(8, "_", "::") == "a::b::c::d");
		assert(l_multi.get_element_at(9, "_", "::") == "a::b::c::d");

		// 9. Leading and trailing scope
		line l_leading("::abort()");
		assert(l_leading.get_element_at(0, "_", "::") == "::abort");
		assert(l_leading.get_element_at(4, "_", "::") == "::abort");
		assert(l_leading.get_element_at(7, "_", "::") == "(");

		line l_trailing("foo::");
		assert(l_trailing.get_element_at(1, "_", "::") == "foo::");
		assert(l_trailing.get_element_at(4, "_", "::") == "foo::");

		line l_nonword_prev("=::foo");
		assert(l_nonword_prev.get_element_at(4, "_", "::") == "::foo");

		line l_nonword_next("foo::=");
		assert(l_nonword_next.get_element_at(1, "_", "::") == "foo::");

		// 10. Spaces around scope delimiter prevent cross-space expansion
		line l_spaces("std :: string");
		assert(l_spaces.get_element_at(1, "_", "::") == "std");
		assert(l_spaces.get_element_at(5, "_", "::") == "::");
		assert(l_spaces.get_element_at(9, "_", "::") == "string");

		// 11. Custom delimiter (e.g. "." in Python/Java)
		line l_dot("os.path.join()");
		assert(l_dot.get_element_at(4, "_", ".") == "os.path.join");

		// 12. Standalone scope delimiter without adjacent words
		line l_isolated(" :: ");
		auto sp_isolated = l_isolated.get_element_span_at(2, "_", "::");
		assert(sp_isolated.word == "::");
		assert(sp_isolated.kind == span_kind::delimiter);
	}

	std::cout << "Testing self-merge...\n";
	line l_merge("hello");
	l_merge.merge(l_merge);
	assert(l_merge.get_text() == "hello");

	std::cout << "Testing concurrent display_col_to_char_pos vs writer...\n";
	line l_concurrent("hello world, this is a test line for concurrent display col");
	std::atomic<bool> stop{false};
	std::thread writer([&]() {
		while (!stop) {
			l_concurrent.set_attributes({syntax_attribute::normal, syntax_attribute::keyword});
			std::this_thread::yield();
		}
	});

	for (int i = 0; i < 500; ++i) {
		int pos = l_concurrent.display_col_to_char_pos(10);
		(void)pos;
	}
	stop = true;
	writer.join();

	std::cout << "line unit test passed!\n";
	return 0;
}
