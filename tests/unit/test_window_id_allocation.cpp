// Tested source file: src/editor.cpp
#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>
#include <vector>
#include "editor.h"
#include "project_manager.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	editor_options opts;
	opts.exit_immediately = 0.0;
	opts.no_welcome = true;
	editor ed(opts);

	// Initially, 1 untitled window is opened with ID 1
	const auto &windows = ed.get_windows();
	assert(windows.size() == 1);
	assert(windows[0]->get_id() == 1);
	assert(ed.get_new_window_id() == 2);

	// Open two more text files
	editor_event open_ev2;
	open_ev2.type = event_type::open_file;
	open_ev2.payload = "doc2.txt";
	ed.dispatch(open_ev2);

	editor_event open_ev3;
	open_ev3.type = event_type::open_file;
	open_ev3.payload = "doc3.txt";
	ed.dispatch(open_ev3);

	assert(windows.size() == 3);
	assert(windows[0]->get_id() == 1);
	assert(windows[1]->get_id() == 2);
	assert(windows[2]->get_id() == 3);
	assert(ed.get_new_window_id() == 4);

	// Close window index 1 (ID 2).
	// Select window 1, then close it.
	editor_event sel_ev;
	sel_ev.type = event_type::select_window;
	sel_ev.key_code = 1;
	ed.dispatch(sel_ev);

	editor_event close_ev;
	close_ev.type = event_type::close_window;
	close_ev.key_code = 1; // 1 = skip unsaved prompt
	ed.dispatch(close_ev);

	// We now have 2 windows left: ID 1 and ID 3.
	assert(windows.size() == 2);
	assert(windows[0]->get_id() == 1);
	assert(windows[1]->get_id() == 3);

	// The lowest free window ID must now be 2 (recycled from the closed window)
	assert(ed.get_new_window_id() == 2);

	// Open a 4th document
	editor_event open_ev4;
	open_ev4.type = event_type::open_file;
	open_ev4.payload = "doc4.txt";
	ed.dispatch(open_ev4);

	assert(windows.size() == 3);

	// Check for duplicate IDs across all windows.
	// In the buggy implementation, windows_.size() + 1 produced ID 3, creating a duplicate ID 3!
	// With the fix, the new window must receive the recycled lowest available ID 2.
	std::unordered_set<int> ids;
	for (const auto &win : windows) {
		assert(win != nullptr);
		assert(!ids.contains(win->get_id()) && "Duplicate window ID detected!");
		ids.insert(win->get_id());
	}
	assert(ids.size() == 3);
	assert(windows[2]->get_id() == 2);

	// Next free ID should be 4 (1, 2, 3 in use)
	assert(ed.get_new_window_id() == 4);

	// Now close the window with ID 1 (which is at index 0)
	editor_event sel_ev0;
	sel_ev0.type = event_type::select_window;
	sel_ev0.key_code = 0;
	ed.dispatch(sel_ev0);

	editor_event close_ev0;
	close_ev0.type = event_type::close_window;
	close_ev0.key_code = 1;
	ed.dispatch(close_ev0);

	// Remaining windows should have IDs 3 and 2
	assert(windows.size() == 2);
	// Lowest free ID must now be 1
	assert(ed.get_new_window_id() == 1);

	// Open doc5: should get recycled ID 1
	editor_event open_ev5;
	open_ev5.type = event_type::open_file;
	open_ev5.payload = "doc5.txt";
	ed.dispatch(open_ev5);

	assert(windows.size() == 3);
	assert(windows.back()->get_id() == 1);

	// Now IDs in use are 3, 2, 1. Lowest free must be 4
	assert(ed.get_new_window_id() == 4);

	// Open doc6: should get ID 4
	editor_event open_ev6;
	open_ev6.type = event_type::open_file;
	open_ev6.payload = "doc6.txt";
	ed.dispatch(open_ev6);

	assert(windows.size() == 4);
	assert(windows.back()->get_id() == 4);

	// Verify all 4 window IDs are distinct (1, 2, 3, 4)
	std::unordered_set<int> final_ids;
	for (const auto &win : windows) {
		assert(win != nullptr);
		assert(!final_ids.contains(win->get_id()));
		final_ids.insert(win->get_id());
	}
	assert(final_ids.size() == 4);
	assert(final_ids.contains(1));
	assert(final_ids.contains(2));
	assert(final_ids.contains(3));
	assert(final_ids.contains(4));
	assert(ed.get_new_window_id() == 5);

	std::cout << "test_window_id_allocation passed successfully!" << std::endl;
	return 0;
}
