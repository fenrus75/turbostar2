// Tested source file: src/editor.cpp
#include <cassert>
#include <iostream>
#include "editor.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(10);

	// Standard ASCII with caret
	assert(editor::get_hotkey_printed_len("^B:Beg") == 5);
	assert(editor::get_hotkey_printed_len("^J:Format") == 8);
	assert(editor::get_hotkey_printed_len("Plain") == 5);
	assert(editor::get_hotkey_printed_len("") == 0);
	assert(editor::get_hotkey_printed_len("^") == 0);

	// Multibyte UTF-8 characters:
	// "Début" has 5 characters ('D', 'é', 'b', 'u', 't'), but 'é' is 2 bytes (0xC3 0xA9).
	// With '^', the visible length must be 1 + 5 = 6 characters (hotkey + label, excluding '^', so 1 ('B') + 1 (':') + 5 ('Début') = 7).
	assert(editor::get_hotkey_printed_len("^B:Début") == 7);
	// "Format…" has '…' which is 3 bytes (0xE2 0x80 0xA6).
	// Length should be 1 ('J') + 1 (':') + 7 ("Format…") = 9.
	assert(editor::get_hotkey_printed_len("^J:Format…") == 9);

	std::cout << "All hotkey printed length tests passed!\n";
	return 0;
}
