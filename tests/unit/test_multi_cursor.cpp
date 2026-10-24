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

		// First Ctrl-D: selects occurrence under cursor
		doc.select_next_scope_highlight();
		assert(doc.has_multi_cursor());
		auto carets = doc.get_multi_cursors();
		assert(carets.size() == 1);
		assert(carets[0].line == 1);
		assert(carets[0].sel_start == 8);
		assert(carets[0].sel_end == 13);
		assert(carets[0].has_selection());

		// Second Ctrl-D: adds line 2 occurrence (count += 1)
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 2);
		assert(carets[0].line == 1);
		assert(carets[1].line == 2);

		// Third Ctrl-D: adds line 3 occurrence (return count)
		doc.select_next_scope_highlight();
		carets = doc.get_multi_cursors();
		assert(carets.size() == 3);
		assert(carets[2].line == 3);

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

		// Type replacement letters: "t", "m", "p"
		doc.insert_char_multi_cursor("t");
		doc.insert_char_multi_cursor("m");
		doc.insert_char_multi_cursor("p");

		assert(doc.get_line(0)->get_text() == "int tmp = 1;");
		assert(doc.get_line(1)->get_text() == "tmp = tmp + 2;");

		// Undo: should revert to original state
		doc.undo();
		assert(doc.get_line(0)->get_text() == "int x = 1;");
		assert(doc.get_line(1)->get_text() == "x = x + 2;");
	}

	// Test 5: Backspace and Delete in multi-cursor mode
	{
		document doc(queue);
		doc.insert_text("cat\ncat\n");

		doc.set_cursor_position(1, 0);
		doc.select_all_scope_highlights();

		// Carets currently have selection over "cat"
		// Backspace deletes selection
		doc.backspace_multi_cursor();
		assert(doc.get_line(0)->get_text() == "");
		assert(doc.get_line(1)->get_text() == "");

		// Type "dog"
		doc.insert_char_multi_cursor("d");
		doc.insert_char_multi_cursor("o");
		doc.insert_char_multi_cursor("g");
		assert(doc.get_line(0)->get_text() == "dog");
		assert(doc.get_line(1)->get_text() == "dog");

		// Backspace without selection deletes last character 'g'
		doc.backspace_multi_cursor();
		assert(doc.get_line(0)->get_text() == "do");
		assert(doc.get_line(1)->get_text() == "do");

		// Move left and delete
		doc.move_multi_cursor(-1); // cursor between 'd' and 'o'
		doc.delete_char_multi_cursor(); // deletes 'o'
		assert(doc.get_line(0)->get_text() == "d");
		assert(doc.get_line(1)->get_text() == "d");
	}

	// Test 6: Arrow navigation clears selection range
	{
		document doc(queue);
		doc.insert_text("item = 1;\nitem = 2;\n");
		doc.set_cursor_position(1, 0);
		doc.select_all_scope_highlights();
		assert(doc.get_multi_cursors()[0].has_selection());

		doc.move_multi_cursor(1); // Right arrow collapses selection to end
		assert(!doc.get_multi_cursors()[0].has_selection());
		assert(doc.get_multi_cursors()[0].col == 4);

		doc.move_multi_cursor(-1); // Left arrow moves cursor backwards
		assert(doc.get_multi_cursors()[0].col == 3);
	}

	std::cout << "All multi-cursor unit tests passed successfully!\n";
	return 0;
}
