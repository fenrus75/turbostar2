#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/components/ui_multiline_edit.h"

int main()
{
	// 1. Instantiation and basic properties
	ui_multiline_edit edit("test_edit", 0, 0, 10, 3, nullptr);
	edit.set_buffer("hello world");

	assert(edit.get_buffer() == "hello world");

	// 2. Word wrapping coordinates verification
	// Width is 10, prefix is 2, so W = 8.
	// "hello world" ->
	// Line 0: "hello " (length 6, start 0)
	// Line 1: "world" (length 5, start 6)
	// Let's call update_scroll to run wrapping (which is done by set_buffer automatically).
	// We'll inspect internal wrapping behavior via pos_to_coord and coord_to_pos helper methods.
	
	// Helper definitions:
	// pos_to_coord(size_t pos, size_t &line_idx, size_t &col)
	// coord_to_pos(size_t line_idx, size_t col)
	
	// Let's test pos_to_coord mapping
	{
		size_t line = 99, col = 99;
		edit.pos_to_coord(0, line, col);
		assert(line == 0 && col == 0); // start of first line
		
		edit.pos_to_coord(5, line, col);
		assert(line == 0 && col == 5); // space after "hello"
		
		edit.pos_to_coord(6, line, col);
		assert(line == 1 && col == 0); // start of second line "world"
		
		edit.pos_to_coord(11, line, col);
		assert(line == 1 && col == 5); // end of second line
	}

	// Let's test coord_to_pos mapping
	{
		assert(edit.coord_to_pos(0, 0) == 0);
		assert(edit.coord_to_pos(0, 5) == 5);
		assert(edit.coord_to_pos(1, 0) == 6);
		assert(edit.coord_to_pos(1, 5) == 11);
		
		// Clamp coordinate column test
		assert(edit.coord_to_pos(1, 10) == 11); // Clamped to line end
	}

	// 3. Navigation key events (Up/Down arrow)
	// Initial cursor is at end of buffer (pos 11, line 1, col 5)
	assert(edit.get_buffer().length() == 11);
	
	// Move cursor up: line 1, col 5 -> line 0, col 5
	editor_event ev_up;
	ev_up.type = event_type::key_press;
	ev_up.key_code = KEY_UP;
	bool handled_up = edit.handle_event(ev_up, 0, 0);
	assert(handled_up);
	
	size_t cur_line = 99, cur_col = 99;
	// Verify cursor is on line 0, column 5 (pos 5)
	edit.pos_to_coord(edit.coord_to_pos(0, 5), cur_line, cur_col); // wait, let's verify actual cursor position in edit
	// Wait, we need access to cursor_pos_, but we don't have a direct getter.
	// We can test if navigation worked by checking coordinates of cursor_pos_ if we map it,
	// or we can add a simple helper, but wait! We can verify cursor position by typing a char
	// and checking the buffer!
	// E.g., if we insert a char, it goes to cursor_pos_.
	// Let's see: we moved up. Now type 'x' at cursor.
	editor_event ev_type_x;
	ev_type_x.type = event_type::key_press;
	ev_type_x.key_code = 'x';
	edit.handle_event(ev_type_x, 0, 0);
	// If cursor was at pos 5, buffer should be "hellox world"
	assert(edit.get_buffer() == "hellox world");

	// Move down: cursor at pos 6 (right after 'x'). Coordinates: line 1, col 0.
	// Move down should go to line 1, col 0 (which is the same column visual-wise as 'x').
	// Let's press Down
	editor_event ev_down;
	ev_down.type = event_type::key_press;
	ev_down.key_code = KEY_DOWN;
	bool handled_down = edit.handle_event(ev_down, 0, 0);
	assert(handled_down);
	// Type 'y'
	editor_event ev_type_y;
	ev_type_y.type = event_type::key_press;
	ev_type_y.key_code = 'y';
	edit.handle_event(ev_type_y, 0, 0);
	// If cursor was at pos 6 (line 0, col 6), moving down to line 1 (length 5) clamps to col 5.
	// So cursor goes to pos 12, typing 'y' results in "hellox worldy"
	assert(edit.get_buffer() == "hellox worldy");

	// 4. Word-by-word horizontal navigation (Ctrl-X/Ctrl-Z)
	edit.set_buffer("one two three");
	// Cursor is at pos 13 (end of buffer)
	// Ctrl-Z (26) is previous word:
	editor_event ev_ctrl_z;
	ev_ctrl_z.type = event_type::key_press;
	ev_ctrl_z.key_code = 26;
	bool handled_ctrl_z = edit.handle_event(ev_ctrl_z, 0, 0);
	assert(handled_ctrl_z);
	// Cursor should move to start of "three" (pos 8)
	// Let's type 'x'
	edit.handle_event(ev_type_x, 0, 0);
	assert(edit.get_buffer() == "one two xthree");

	// Ctrl-X (24) is next word:
	editor_event ev_ctrl_x;
	ev_ctrl_x.type = event_type::key_press;
	ev_ctrl_x.key_code = 24;
	bool handled_ctrl_x = edit.handle_event(ev_ctrl_x, 0, 0);
	assert(handled_ctrl_x);
	// Cursor should move to end of "three" (pos 14)
	edit.handle_event(ev_type_y, 0, 0);
	assert(edit.get_buffer() == "one two xthreey");

	// 5. Home and End on visual lines
	edit.set_buffer("hello world"); // W = 8, wraps to: line 0 "hello ", line 1 "world"
	// Set buffer sets cursor to end (pos 11, line 1, col 5)
	// Send Home: should go to start of line 1 (pos 6)
	editor_event ev_home;
	ev_home.type = event_type::key_press;
	ev_home.key_code = KEY_HOME;
	bool handled_home = edit.handle_event(ev_home, 0, 0);
	assert(handled_home);
	edit.handle_event(ev_type_x, 0, 0);
	assert(edit.get_buffer() == "hello xworld");

	// Send End: should go to end of line 1 (pos 12)
	editor_event ev_end;
	ev_end.type = event_type::key_press;
	ev_end.key_code = KEY_END;
	bool handled_end = edit.handle_event(ev_end, 0, 0);
	assert(handled_end);
	edit.handle_event(ev_type_y, 0, 0);
	assert(edit.get_buffer() == "hello xworldy");

	// 6. Selection: Modern Shift-Selection
	edit.set_buffer("hello world");
	// Cursor at 11. Press Shift-Left (KEY_SLEFT = 393)
	editor_event ev_sleft;
	ev_sleft.type = event_type::key_press;
	ev_sleft.key_code = KEY_SLEFT;
	bool handled_sleft = edit.handle_event(ev_sleft, 0, 0);
	assert(handled_sleft);
	// Type 'a' (should delete selection "d" and replace with 'a')
	editor_event ev_type_a;
	ev_type_a.type = event_type::key_press;
	ev_type_a.key_code = 'a';
	edit.handle_event(ev_type_a, 0, 0);
	assert(edit.get_buffer() == "hello worla");

	// 7. Selection: Wordstar Selection
	edit.set_buffer("hello world");
	// Cursor at 11. Move cursor left to pos 6 ("world").
	// We do 5 KEY_LEFT presses
	editor_event ev_left;
	ev_left.type = event_type::key_press;
	ev_left.key_code = KEY_LEFT;
	for (int i = 0; i < 5; ++i) {
		edit.handle_event(ev_left, 0, 0);
	}
	
	// Set selection begin: Ctrl-K (11), then 'B'
	editor_event ev_ctrl_k;
	ev_ctrl_k.type = event_type::key_press;
	ev_ctrl_k.key_code = 11;
	edit.handle_event(ev_ctrl_k, 0, 0);
	
	editor_event ev_b;
	ev_b.type = event_type::key_press;
	ev_b.key_code = 'B';
	edit.handle_event(ev_b, 0, 0);

	// Move cursor right to pos 11
	editor_event ev_right;
	ev_right.type = event_type::key_press;
	ev_right.key_code = KEY_RIGHT;
	for (int i = 0; i < 5; ++i) {
		edit.handle_event(ev_right, 0, 0);
	}

	// Set selection end: Ctrl-K, then 'K'
	edit.handle_event(ev_ctrl_k, 0, 0);
	editor_event ev_k;
	ev_k.type = event_type::key_press;
	ev_k.key_code = 'K';
	edit.handle_event(ev_k, 0, 0);

	// Delete selection: Ctrl-K, then 'Y'
	edit.handle_event(ev_ctrl_k, 0, 0);
	editor_event ev_y;
	ev_y.type = event_type::key_press;
	ev_y.key_code = 'Y';
	edit.handle_event(ev_y, 0, 0);

	// "world" should be deleted, leaving "hello "
	assert(edit.get_buffer() == "hello ");

	std::cout << "ui_multiline_edit unit tests passed!\n";
	return 0;
}
