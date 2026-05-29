#include <cassert>
#include <iostream>
#include "../../src/line.h"

int main()
{
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
	// Basic assumption: multi-byte chars are 1 cell wide.
	// So: 'a' at col 0, 'あ' at col 1, '😊' at col 2, 'b' at col 3, end at col 4
	assert(l.char_to_display_col(0) == 0);
	assert(l.char_to_display_col(1) == 1);
	assert(l.char_to_display_col(2) == 2);
	assert(l.char_to_display_col(3) == 3);
	assert(l.char_to_display_col(4) == 4);

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

	std::cout << "line unit test passed!\n";
	return 0;
}
