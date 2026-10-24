// Tested source file: src/document_multicursor.cpp
#include "test_watchdog.h"

#include <cassert>
#include <iostream>
#include <string>
#include "document.h"
#include "event_queue.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	event_queue queue;

	// Test 1: Progressive multi-cursor selection with select_next_scope_highlight (Ctrl+D)
	{
		document doc(queue);
		// Prepare a small function scope with multiple occurrences of a variable
		doc.insert_text("void test() {\n    int count = 0;\n    count += 1;\n    return count;\n}\n");

		// Move cursor to "count" on line 2 (0-indexed: line 1)
		doc.set_cursor_position(10, 1);
		assert(doc.get_word_under_cursor() == "count");
		assert(!doc.has_multi_cursor());

		// First Ctrl-D: selects occurrence under cursor as a point caret at cursor offset
		doc.select_next_scope_highlight();
		assert(doc.has_multi_cursor());
		auto carets = doc.get_multi_cursors();
		assert(carets.size() == 1);
		assert(carets[0].line == 1);
		assert(carets[0].col == 10);
		assert(!carets[0].has_selection());

		// Second Ctrl-D: adds line 2 occurrence (count += 1, col 4 + 2 = 6)
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 2);
		assert(carets[0].line == 1);
		assert(carets[1].line == 2);
		assert(carets[1].col == 6);
		assert(!carets[1].has_selection());

		// Third Ctrl-D: adds line 3 occurrence (return count, col 11 + 2 = 13)
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 3);
		assert(carets[2].line == 3);
		assert(carets[2].col == 13);
		assert(!carets[2].has_selection());

		// Fourth Ctrl-D: all occurrences in scope selected
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 3);

		// Clear multi-cursor (Esc)
		doc.clear_multi_cursor();
		assert(!doc.has_multi_cursor());
		assert(doc.get_multi_cursors().empty());
	}

	// Test 2: Wrap-around selection when starting from middle occurrence
	{
		document doc(queue);
		doc.insert_text("int val = 1;\nval += 2;\nreturn val;\n");

		// Cursor on line 1 ("val += 2")
		doc.set_cursor_position(1, 1);
		assert(doc.get_word_under_cursor() == "val");

		// 1st Ctrl+D selects line 1
		doc.select_next_scope_highlight();
		auto carets = doc.get_multi_cursors();
		assert(carets.size() == 1);
		assert(carets[0].line == 1);

		// 2nd Ctrl+D selects line 2 (forward)
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 2);
		assert(carets[1].line == 2);

		// 3rd Ctrl+D wraps around to line 0!
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 3);
		assert(carets[0].line == 0);
		assert(carets[1].line == 1);
		assert(carets[2].line == 2);
	}

	// Test 3: Select all in scope (Ctrl+Shift+D / Ctrl+Shift+L)
	{
		document doc(queue);
		doc.insert_text("int foo = 10;\nfoo = foo + 5;\nreturn foo;\n");

		doc.set_cursor_position(5, 0);
		assert(doc.get_word_under_cursor() == "foo");

		doc.select_all_scope_highlights();
		assert(doc.has_multi_cursor());
		auto carets = doc.get_multi_cursors();
		// Line 0: 1 'foo', Line 1: 2 'foo's, Line 2: 1 'foo' -> total 4
		assert(carets.size() == 4);
		assert(carets[0].line == 0);
		assert(carets[1].line == 1);
		assert(carets[2].line == 1);
		assert(carets[3].line == 2);
	}

	// Test 4: Concurrent replacement typing & compound undo
	{
		document doc(queue);
		doc.insert_text("int x = 1;\nx = x + 2;\n");

		doc.set_cursor_position(4, 0);
		doc.select_all_scope_highlights();
		assert(doc.get_multi_cursors().size() == 3);

		// Delete 'x' across all carets
		doc.delete_char_multi_cursor();

		// Type replacement letters: "t", "m", "p"
		doc.insert_char_multi_cursor("t");
		doc.insert_char_multi_cursor("m");
		doc.insert_char_multi_cursor("p");

		assert(doc.get_line(0)->get_text() == "int tmp = 1;");
		assert(doc.get_line(1)->get_text() == "tmp = tmp + 2;");

		// Undo typing: should revert to empty variable name before typing
		doc.undo();
		assert(doc.get_line(0)->get_text() == "int  = 1;");
		assert(doc.get_line(1)->get_text() == " =  + 2;");

		// Undo delete: should revert to original state
		doc.undo();
		assert(doc.get_line(0)->get_text() == "int x = 1;");
		assert(doc.get_line(1)->get_text() == "x = x + 2;");
	}

	// Test 5: Backspace and Delete in multi-cursor mode
	{
		document doc(queue);
		doc.insert_text("dog\ndog\n");

		// Cursor at col 3 (end of "dog")
		doc.set_cursor_position(3, 0);
		doc.select_all_scope_highlights();

		// Backspace deletes 'g'
		doc.backspace_multi_cursor();
		assert(doc.get_line(0)->get_text() == "do");
		assert(doc.get_line(1)->get_text() == "do");

		// Type "ts"
		doc.insert_char_multi_cursor("t");
		doc.insert_char_multi_cursor("s");
		assert(doc.get_line(0)->get_text() == "dots");
		assert(doc.get_line(1)->get_text() == "dots");

		// Move left 1 character and delete 's'
		doc.move_multi_cursor(-1); // cursor between 't' and 's'
		doc.delete_char_multi_cursor(); // deletes 's'
		assert(doc.get_line(0)->get_text() == "dot");
		assert(doc.get_line(1)->get_text() == "dot");
	}

	// Test 6: Arrow navigation steps by single characters
	{
		document doc(queue);
		doc.insert_text("item = 1;\nitem = 2;\n");
		doc.set_cursor_position(1, 0); // index 1: i|tem
		doc.select_all_scope_highlights();
		assert(doc.get_multi_cursors()[0].col == 1);

		doc.move_multi_cursor(1); // Right arrow moves 1 character right
		assert(doc.get_multi_cursors()[0].col == 2);

		doc.move_multi_cursor(-1); // Left arrow moves 1 character left
		assert(doc.get_multi_cursors()[0].col == 1);
	}

	// Test 7: User scenario - placing cursor near beginning of multi-word, moving right 1 char, typing to insert
	{
		document doc(queue);
		doc.insert_text("my_var = 1;\nmy_var = 2;\n");

		// Cursor at index 2 (between "my" and "_var")
		doc.set_cursor_position(2, 0);
		doc.select_all_scope_highlights();

		// Press right arrow: must move to index 3, NOT jump to end of word!
		doc.move_multi_cursor(1);
		assert(doc.get_multi_cursors()[0].col == 3);
		assert(doc.get_multi_cursors()[1].col == 3);

		// Type 'x' to insert into the word: must add letter without replacing the whole word!
		doc.insert_char_multi_cursor("x");
		assert(doc.get_line(0)->get_text() == "my_xvar = 1;");
		assert(doc.get_line(1)->get_text() == "my_xvar = 2;");
	}

	std::cout << "All multi-cursor unit tests passed successfully!\n";
	return 0;
}
