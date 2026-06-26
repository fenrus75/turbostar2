#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/document.h"
#include "../../src/event_queue.h"
#include "../../src/ui/window.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	event_queue global_queue;

	// Create document and fill it with lines to scroll
	auto doc = std::make_shared<document>(global_queue);
	for (int i = 0; i < 50; ++i) {
		doc->append_line("Line " + std::to_string(i) + " is a very long line to test horizontal scrolling in the editor!");
	}
	doc->clear_modified();

	// Create a window at x=0, y=0, w=40, h=10
	// Viewport height will be height - 2 = 8
	// Viewport width will be width - 2 = 38
	window win(1, 0, 0, 40, 10, "Test Window");
	win.attach_document(doc);

	// Explicitly reset cursor to 0,0
	doc->move_cursor(-doc->get_cursor_x(), -doc->get_cursor_y());
	assert(doc->get_cursor_y() == 0);
	assert(doc->get_cursor_x() == 0);

	// Test 1: Click on the bottom scrollbar (row y + height - 1 = 9)
	// This should trigger horizontal scrolling.
	// Coordinate (x=20, y=9) represents a click in the track of the horizontal scrollbar.
	{
		editor_event ev;
		ev.type = event_type::mouse_click;
		ev.mouse_x = 20;
		ev.mouse_y = 9; // bottom scrollbar
		win.get_window_queue().push(ev);
		win.process_events();

		// Cursor Y must not change.
		assert(doc->get_cursor_y() == 0);
	}

	// Test 2: Click on the right scrollbar track (col x + width - 1 = 39, row 5)
	// This should trigger vertical scrolling.
	{
		editor_event ev;
		ev.type = event_type::mouse_click;
		ev.mouse_x = 39;
		ev.mouse_y = 5; // right scrollbar track
		win.get_window_queue().push(ev);
		win.process_events();

		// Cursor Y should have changed since it scrolled vertically.
		assert(doc->get_cursor_y() > 0);
	}

	std::cout << "Test window scrollbar click passed!\n";
	return 0;
}
