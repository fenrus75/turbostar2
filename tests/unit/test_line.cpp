#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <clocale>
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

	std::cout << "line unit test passed!\n";
	return 0;
}
