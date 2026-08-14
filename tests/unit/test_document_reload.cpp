// test_document_reload.cpp
//
// Unit test for document cursor_state capture and line-matching restoration upon reload.

#include "test_watchdog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include "document.h"
#include "event_queue.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	event_queue queue;
	std::string test_file = "test_doc_reload_tmp.txt";

	// Write initial file content
	{
		std::ofstream out(test_file);
		out << "line 1\n";
		out << "line 2\n";
		out << "line 3 target\n";
		out << "line 4\n";
		out << "line 5\n";
	}

	document doc(queue, test_file);
	assert(doc.get_line_count() == 5);

	// Position cursor at line 3 ("line 3 target"), char 5
	doc.set_cursor_position(5, 2);
	assert(doc.get_cursor_y() == 2);
	assert(doc.get_cursor_x() == 5);

	// Capture cursor state
	cursor_state st = doc.capture_cursor_state();
	assert(st.cursor_y == 2);
	assert(st.cursor_x == 5);
	assert(st.current_line_text == "line 3 target");

	// Modify file on disk: insert 2 lines above "line 1"
	{
		std::ofstream out(test_file);
		out << "header A\n";
		out << "header B\n";
		out << "line 1\n";
		out << "line 2\n";
		out << "line 3 target\n";
		out << "line 4\n";
		out << "line 5\n";
	}

	// Reload from file
	bool reloaded = doc.load_from_file(test_file);
	assert(reloaded);
	assert(doc.get_line_count() == 7);

	// Verify line-matching cursor shift: cursor_y should now be 4 (shifted by +2)
	assert(doc.get_cursor_y() == 4);
	assert(doc.get_cursor_x() == 5);
	assert(doc.get_line(doc.get_cursor_y())->get_text() == "line 3 target");

	// Test fallback clamping when line text is completely replaced
	{
		std::ofstream out(test_file);
		out << "completely new content 1\n";
		out << "completely new content 2\n";
	}
	reloaded = doc.load_from_file(test_file);
	assert(reloaded);
	assert(doc.get_line_count() == 2);
	assert(doc.get_cursor_y() == 1); // Clamped to line index 1

	// Cleanup
	std::remove(test_file.c_str());

	std::cout << "test_document_reload passed cleanly!" << std::endl;
	return 0;
}
