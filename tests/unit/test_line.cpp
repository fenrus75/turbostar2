// Tested source file: src/line.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <atomic>
#include <thread>
#include "../../src/line.h"

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
