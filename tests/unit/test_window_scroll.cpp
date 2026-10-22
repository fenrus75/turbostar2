// Tested source file: src/ui/window.cpp, src/editor.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <vector>
#include "document.h"
#include "editor.h"
#include "event_queue.h"
#include "ui/window.h"

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

	// Test 3: Null safety when sorting windows for display with null active window and null entries
	{
		window win1(1, 0, 0, 20, 10, "Win 1");
		window win2(2, 0, 0, 20, 10, "Win 2");
		std::vector<window *> windows = {nullptr, &win1, nullptr, &win2, nullptr};
		editor::sort_windows_by_priority(windows, nullptr);
		assert(windows.size() == 2);
		assert(windows[0] != nullptr && windows[1] != nullptr);
	}

	// Test 4: Mouse click and drag does not alter or clear document block selection
	{
		doc->set_selection(1, 0, 3, 5);
		assert(doc->has_selection());

		// Mouse click inside content
		editor_event ev_click;
		ev_click.type = event_type::mouse_click;
		ev_click.mouse_x = 5;
		ev_click.mouse_y = 5;
		win.get_window_queue().push(ev_click);
		win.process_events();

		// Document block selection MUST remain intact!
		assert(doc->has_selection());
		int sx = 0, sy = 0, ex = 0, ey = 0;
		doc->get_selection_range(sx, sy, ex, ey);
		assert(sy == 1 && sx == 0 && ey == 3 && ex == 5);

		// Mouse drag inside content
		editor_event ev_drag;
		ev_drag.type = event_type::mouse_drag;
		ev_drag.mouse_x = 10;
		ev_drag.mouse_y = 6;
		win.get_window_queue().push(ev_drag);
		win.process_events();

		// Document block selection MUST still remain intact!
		assert(doc->has_selection());
		doc->get_selection_range(sx, sy, ex, ey);
		assert(sy == 1 && sx == 0 && ey == 3 && ex == 5);

		// Mouse release
		editor_event ev_rel;
		ev_rel.type = event_type::mouse_release;
		ev_rel.mouse_x = 10;
		ev_rel.mouse_y = 6;
		win.get_window_queue().push(ev_rel);
		win.process_events();

		assert(doc->has_selection());
		doc->get_selection_range(sx, sy, ex, ey);
		assert(sy == 1 && sx == 0 && ey == 3 && ex == 5);
		assert(win.has_mouse_selection());

		// Keypress dismisses mouse selection while preserving block selection
		editor_event ev_key;
		ev_key.type = event_type::key_press;
		ev_key.key_code = 'a';
		ev_key.utf8_char = "a";
		win.get_window_queue().push(ev_key);
		win.process_events();

		assert(!win.has_mouse_selection());
		assert(doc->has_selection());

		// Test mouse drag past bottom boundary triggers auto-scroll
		int prev_top = win.get_scroll_top_line();
		editor_event ev_drag_down;
		ev_drag_down.type = event_type::mouse_click;
		ev_drag_down.mouse_x = 5;
		ev_drag_down.mouse_y = 5;
		win.get_window_queue().push(ev_drag_down);
		win.process_events();

		ev_drag_down.type = event_type::mouse_drag;
		ev_drag_down.mouse_x = 5;
		ev_drag_down.mouse_y = 15; // past bottom of height 10
		win.get_window_queue().push(ev_drag_down);
		win.process_events();

		assert(win.get_scroll_top_line() > prev_top);
	}

	// Test 5: Word navigation with KEY_CRIGHT and KEY_CLEFT
	{
		doc->clear();
		doc->append_line("hello world test");
		doc->move_cursor(-doc->get_cursor_x(), -doc->get_cursor_y());
		assert(doc->get_cursor_x() == 0);

		editor_event ev_cright;
		ev_cright.type = event_type::key_press;
		ev_cright.key_code = KEY_CRIGHT;
		win.get_window_queue().push(ev_cright);
		win.process_events();
		assert(doc->get_cursor_x() == 6); // start of "world"

		win.get_window_queue().push(ev_cright);
		win.process_events();
		assert(doc->get_cursor_x() == 12); // start of "test"

		editor_event ev_cleft;
		ev_cleft.type = event_type::key_press;
		ev_cleft.key_code = KEY_CLEFT;
		win.get_window_queue().push(ev_cleft);
		win.process_events();
		assert(doc->get_cursor_x() == 6); // back to start of "world"

		win.get_window_queue().push(ev_cleft);
		win.process_events();
		assert(doc->get_cursor_x() == 0); // back to start of "hello"
	}

	// Test 6: Mouse double click word, whitespace, and delimiter selection
	{
		auto doc6 = std::make_shared<document>(global_queue);
		doc6->append_line("hello   world «bonjour» foo::bar ===");
		// 0..4: "hello"
		// 5..7: "   "
		// 8..12: "world"
		// 13: ' '
		// 14: '«'
		// 15..21: "bonjour"
		// 22: '»'
		// 23: ' '
		// 24..26: "foo"
		// 27..28: "::"
		// 29..31: "bar"
		// 32: ' '
		// 33..35: "==="

		window win6(2, 0, 0, 80, 24, "Double Click Window");
		win6.attach_document(doc6);

		auto click_and_assert = [&](int char_pos, const std::string &expected) {
			editor_event ev_dbl;
			ev_dbl.type = event_type::mouse_double_click;
			ev_dbl.mouse_y = 1; // row 0
			int col = doc6->get_line(0)->char_to_display_col(char_pos);
			ev_dbl.mouse_x = 1 + col;
			win6.get_window_queue().push(ev_dbl);
			win6.process_events();

			assert(win6.has_mouse_selection());
			assert(win6.get_mouse_selected_text() == expected);
		};

		// 1. Double click on "hello"
		click_and_assert(2, "hello");

		// 2. Double click on whitespace "   "
		click_and_assert(6, "   ");

		// 3. Double click on "world"
		click_and_assert(10, "world");

		// 4. Double click on "bonjour" inside «bonjour»
		click_and_assert(16, "bonjour");

		// 5. Double click on "foo" in foo::bar
		click_and_assert(25, "foo");

		// 6. Double click on "::" delimiter in foo::bar
		click_and_assert(27, "::");

		// 7. Double click on "==="
		click_and_assert(34, "===");

		// 8. Double click on CJK word and fullwidth space "日本語　テスト"
		doc6->clear();
		doc6->append_line("日本語　テスト");
		// 0: '日', 1: '本', 2: '語', 3: '　', 4: 'テ', 5: 'ス', 6: 'ト'
		click_and_assert(1, "日本語");
		click_and_assert(3, "　");
		click_and_assert(5, "テスト");
	}

	std::cout << "Test window scrollbar click passed!\n";
	return 0;
}
