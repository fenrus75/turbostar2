#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_buttons_vertical.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_dropdown.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"
#include "input_history_manager.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	bool space_called = false;
	int space_index = -1;

	ui_listbox lb("my_listbox", 0, 0, 10, 5, nullptr, nullptr);
	lb.set_items({"item0", "item1", "item2"});
	lb.set_on_space([&](int idx) {
		space_called = true;
		space_index = idx;
	});

	// Check default selection and get_value
	assert(lb.get_selected_index() == 0);
	auto val = lb.get_value("my_listbox");
	assert(val.has_value());
	assert(*val == "0");

	// Move selection down
	editor_event ev_down;
	ev_down.type = event_type::key_press;
	ev_down.key_code = KEY_DOWN;
	bool handled_down = lb.handle_event(ev_down, 0, 0);
	assert(handled_down);
	assert(lb.get_selected_index() == 1);
	val = lb.get_value("my_listbox");
	assert(*val == "1");

	// Press space key to toggle/submit
	editor_event ev_space;
	ev_space.type = event_type::key_press;
	ev_space.key_code = ' ';
	bool handled_space = lb.handle_event(ev_space, 0, 0);
	assert(handled_space);
	assert(space_called);
	assert(space_index == 1);

	// Try non-matching get_value
	auto no_val = lb.get_value("nonexistent");
	assert(!no_val.has_value());

	// Test ui_element and ui_button natural bounds
	ui_button btn("btn_toggle", 0, 0, " Toggle ", 't', nullptr);
	assert(btn.width() == 9);
	assert(btn.height() == 1);
	assert(btn.natural_width() == 9);
	assert(btn.natural_height() == 1);

	ui_button btn2("btn_close", 0, 0, "Close", 'c', nullptr);
	assert(btn2.width() == 8);
	assert(btn2.height() == 1);
	assert(btn2.natural_width() == 8);
	assert(btn2.natural_height() == 1);

	// Test set_position helper
	btn2.set_position(5, 12);
	assert(btn2.x() == 5);
	assert(btn2.y() == 12);
	assert(btn2.width() == 8);
	assert(btn2.height() == 1);

	// Test set_width and set_height helpers
	btn2.set_width(15);
	btn2.set_height(3);
	assert(btn2.width() == 15);
	assert(btn2.height() == 3);

	// Test flow() helper and propagation
	assert(!btn2.flow());

	class test_element : public ui_element
	{
	      public:
		bool flow_val = false;
		test_element(std::string name) : ui_element(name, 0, 0, 1, 1)
		{
		}
		void draw(int abs_x, int abs_y) const override
		{
			(void)abs_x;
			(void)abs_y;
		}
		bool handle_event(const editor_event &ev, int abs_x, int abs_y) override
		{
			(void)ev;
			(void)abs_x;
			(void)abs_y;
			return false;
		}
		bool flow() override
		{
			return flow_val;
		}
	};

	ui_container container("my_container", 0, 0, 10, 10);
	assert(!container.flow());

	auto child1 = std::make_unique<test_element>("child1");
	auto child2 = std::make_unique<test_element>("child2");
	test_element *c1_ptr = child1.get();
	test_element *c2_ptr = child2.get();

	container.add_child(std::move(child1));
	container.add_child(std::move(child2));

	assert(!container.flow());

	c1_ptr->flow_val = true;
	assert(container.flow());

	c1_ptr->flow_val = false;
	c2_ptr->flow_val = true;
	assert(container.flow());

	c1_ptr->flow_val = true;
	c2_ptr->flow_val = true;
	assert(container.flow());

	// Test ui_buttons_horizontal layout
	ui_buttons_horizontal btns_container("btns_h", 0, 0, 35, 1);
	auto b1 = std::make_unique<ui_button>("b1", 0, 0, "A", 'a', nullptr);
	auto b2 = std::make_unique<ui_button>("b2", 0, 0, "Longer", 'l', nullptr);
	auto b3 = std::make_unique<ui_button>("b3", 0, 0, "Yes", 'y', nullptr);
	ui_button *b1_ptr = b1.get();
	ui_button *b2_ptr = b2.get();
	ui_button *b3_ptr = b3.get();

	btns_container.add_child(std::move(b1));
	btns_container.add_child(std::move(b2));
	btns_container.add_child(std::move(b3));

	// First flow recalculation should return true
	assert(btns_container.flow());

	// Max natural width should be 9 (for "Longer" which has length 6 + 3)
	assert(b1_ptr->width() == 9);
	assert(b2_ptr->width() == 9);
	assert(b3_ptr->width() == 9);

	// Positions should be perfectly spaced out by 2
	assert(b1_ptr->x() == 0);
	assert(b2_ptr->x() == 11); // 0 + 9 + 2
	assert(b3_ptr->x() == 22); // 11 + 9 + 2

	// Container size should match the total width (31) and height (2)
	assert(btns_container.width() == 31);
	assert(btns_container.height() == 2);

	// Subsequent flow calls should return false since layout is settled
	assert(!btns_container.flow());

	// Test ui_buttons_vertical layout
	ui_buttons_vertical btns_v_container("btns_v", 0, 0, 10, 10);
	auto bv1 = std::make_unique<ui_button>("bv1", 0, 0, "A", 'a', nullptr);
	auto bv2 = std::make_unique<ui_button>("bv2", 0, 0, "Longer", 'l', nullptr);
	auto bv3 = std::make_unique<ui_button>("bv3", 0, 0, "Yes", 'y', nullptr);
	ui_button *bv1_ptr = bv1.get();
	ui_button *bv2_ptr = bv2.get();
	ui_button *bv3_ptr = bv3.get();

	btns_v_container.add_child(std::move(bv1));
	btns_v_container.add_child(std::move(bv2));
	btns_v_container.add_child(std::move(bv3));

	// First flow recalculation should return true
	assert(btns_v_container.flow());

	// Max natural width should be 9
	assert(bv1_ptr->width() == 9);
	assert(bv2_ptr->width() == 9);
	assert(bv3_ptr->width() == 9);

	// Positions: X must be 0, Y must increment by 2
	assert(bv1_ptr->x() == 0);
	assert(bv1_ptr->y() == 0);
	assert(bv2_ptr->x() == 0);
	assert(bv2_ptr->y() == 2);
	assert(bv3_ptr->x() == 0);
	assert(bv3_ptr->y() == 4);

	// Container size should match the max natural width (9) and total height (6)
	assert(btns_v_container.width() == 9);
	assert(btns_v_container.height() == 6);

	// Subsequent flow calls should return false
	assert(!btns_v_container.flow());

	// Test mouse click coordinate correctness when x_ and y_ are non-zero
	{
		ui_listbox lb_coords("coords_listbox", 2, 3, 10, 5, nullptr, nullptr);
		lb_coords.set_items({"item0", "item1", "item2"});
		lb_coords.set_selected_index(0);

		editor_event ev_mouse;
		ev_mouse.type = event_type::mouse_click;
		// Absolute position is (2, 3). Clicking at (2, 4) should select item 1 (index 1).
		ev_mouse.mouse_x = 2;
		ev_mouse.mouse_y = 4;

		bool handled_mouse = lb_coords.handle_event(ev_mouse, 2, 3);
		assert(handled_mouse);
		assert(lb_coords.get_selected_index() == 1);
	}

	// Test ui_fileselector focusability
	{
		ui_fileselector fs("my_fileselector", 0, 0, 40, 10, ".", nullptr, nullptr);
		assert(fs.is_focusable());
	}

	// Test ui_checkbox natural width
	{
		ui_checkbox cb("my_checkbox", "Enable LSP", 'E');
		assert(cb.natural_width() == 16); // "Enable LSP" (10) + 6 = 16
	}

	// Test ui_dropdown focusability
	{
		ui_dropdown dd("my_dropdown", 0, 0, 20, "init", {"cand1", "cand2"});
		assert(dd.is_focusable());
	}
	// Test natural dimensions of layout containers
	{
		// 1. ui_buttons_horizontal natural height
		assert(btns_container.natural_height() == 2);

		// 2. ui_checkbox_group
		ui_checkbox_group cbg("cbg");
		auto cb1 = std::make_unique<ui_checkbox>("cb1", "Opt 1", '1');
		auto cb2 = std::make_unique<ui_checkbox>("cb2", "Option Two", '2');
		cbg.add_child(std::move(cb1));
		cbg.add_child(std::move(cb2));
		assert(cbg.natural_width() == 16);
		assert(cbg.natural_height() == 3); // 1 + 1 + 1

		// 3. ui_group_box
		ui_group_box gb("gb", 30, "My Group");
		auto g_child = std::make_unique<ui_checkbox>("g_child", "Hello", 'H'); // nw = 11, nh = 1
		g_child->set_bounds(1, 2, 11, 1);
		gb.add_child(std::move(g_child));
		assert(gb.natural_width() == 13); // max(8, 11+2)
		assert(gb.natural_height() == 3); // 2 + 1 = 3

		// 4. ui_horizontal_flow
		ui_horizontal_flow hflow("hflow", 1, 2);			      // x_offset = 1, y_offset = 2
		auto h1 = std::make_unique<ui_button>("h1", 0, 0, "A", 'a', nullptr); // nw = 4, nh = 1
		auto h2 = std::make_unique<ui_button>("h2", 0, 0, "B", 'b', nullptr); // nw = 4, nh = 1
		hflow.add_child(std::move(h1));
		hflow.add_child(std::move(h2));
		assert(hflow.natural_width() == 12);
		assert(hflow.natural_height() == 5);

		// 5. ui_radiobutton_group
		ui_radiobutton_group rbg("rbg", false);				 // vertical
		auto r1 = std::make_unique<ui_radio_choice>("r1", "Yes", 'y');	 // nw = 7, nh = 1
		auto r2 = std::make_unique<ui_radio_choice>("r2", "Maybe", 'm'); // nw = 9, nh = 1
		rbg.add_child(std::move(r1));
		rbg.add_child(std::move(r2));
		assert(rbg.natural_width() == 9);
		assert(rbg.natural_height() == 3); // 1 + 1 + 1 = 3

		// 6. ui_vertical_flow
		ui_vertical_flow vflow("vflow", 1, 2, 3);			      // x_offset = 1, y_offset = 2, spacer = 3
		auto v1 = std::make_unique<ui_button>("v1", 0, 0, "A", 'a', nullptr); // nw = 4, nh = 1
		auto v2 = std::make_unique<ui_button>("v2", 0, 0, "B", 'b', nullptr); // nw = 4, nh = 1
		vflow.add_child(std::move(v1));
		vflow.add_child(std::move(v2));
		assert(vflow.natural_width() == 6);
		assert(vflow.natural_height() == 9);
	}

	// Test ui_textbox key handling for Ctrl-A (code 1) and Ctrl-E (code 5)
	{
		ui_textbox tb("my_textbox", 40, "hello world");
		tb.set_focus(true);

		// Move cursor to some position in the middle
		editor_event ev_left;
		ev_left.type = event_type::key_press;
		ev_left.key_code = KEY_LEFT;
		tb.handle_event(ev_left, 0, 0);
		tb.handle_event(ev_left, 0, 0);
		auto val = tb.get_value("my_textbox");
		assert(val.has_value() && *val == "hello world");

		// Send Ctrl-A
		editor_event ev_ctrl_a;
		ev_ctrl_a.type = event_type::key_press;
		ev_ctrl_a.key_code = 1; // Ctrl-A
		bool handled_a = tb.handle_event(ev_ctrl_a, 0, 0);
		assert(handled_a);

		// Character typed should be inserted at index 0 (start of string)
		editor_event ev_char;
		ev_char.type = event_type::key_press;
		ev_char.key_code = 'x';
		tb.handle_event(ev_char, 0, 0);
		auto val_after_a = tb.get_value("my_textbox");
		assert(*val_after_a == "xhello world");

		// Send Ctrl-E
		editor_event ev_ctrl_e;
		ev_ctrl_e.type = event_type::key_press;
		ev_ctrl_e.key_code = 5; // Ctrl-E
		bool handled_e = tb.handle_event(ev_ctrl_e, 0, 0);
		assert(handled_e);

		// Character typed should be inserted at the end of the string
		editor_event ev_char_y;
		ev_char_y.type = event_type::key_press;
		ev_char_y.key_code = 'y';
		tb.handle_event(ev_char_y, 0, 0);
		auto val_after_e = tb.get_value("my_textbox");
		assert(*val_after_e == "xhello worldy");
	}

	// Test ui_textbox mouse selection and copying
	{
		ui_textbox tb("my_textbox", 40, "hello world");
		tb.set_focus(true);

		int start = -1, end = -1;
		tb.get_selection_range(start, end);
		assert(start == -1 && end == -1);

		// Click inside textbox to start selection
		editor_event click_ev;
		click_ev.type = event_type::mouse_click;
		click_ev.mouse_x = 2; // click 3rd char ('l')
		click_ev.mouse_y = 0;
		bool handled_click = tb.handle_event(click_ev, 0, 0);
		assert(handled_click);

		tb.get_selection_range(start, end);
		assert(start == 2 && end == 2);

		// Drag to 6th char (' ')
		editor_event drag_ev;
		drag_ev.type = event_type::mouse_drag;
		drag_ev.mouse_x = 5;
		drag_ev.mouse_y = 0;
		bool handled_drag = tb.handle_event(drag_ev, 0, 0);
		assert(handled_drag);

		tb.get_selection_range(start, end);
		assert(start == 2 && end == 5);

		// Release mouse to copy
		editor_event release_ev;
		release_ev.type = event_type::mouse_release;
		release_ev.mouse_x = 5;
		release_ev.mouse_y = 0;
		bool handled_release = tb.handle_event(release_ev, 0, 0);
		assert(handled_release);

		// Verify selection is still tracked (for draw highlight) but drag active state is done
		tb.get_selection_range(start, end);
		assert(start == 2 && end == 5);

		// Key press should clear the selection
		editor_event key_ev;
		key_ev.type = event_type::key_press;
		key_ev.key_code = 'a';
		tb.handle_event(key_ev, 0, 0);
		tb.get_selection_range(start, end);
		assert(start == -1 && end == -1);
	}

	// Test ui_container mouse drag and release propagation
	{
		ui_container container("my_container", 0, 0, 80, 24);
		auto tb_ptr = std::make_unique<ui_textbox>("my_textbox", 0, 0, 40, "hello world");
		ui_textbox *tb = tb_ptr.get();
		container.add_child(std::move(tb_ptr));
		container.set_focus(true);

		// Click inside textbox via container
		editor_event click_ev;
		click_ev.type = event_type::mouse_click;
		click_ev.mouse_x = 2;
		click_ev.mouse_y = 0;
		bool handled_click = container.handle_event(click_ev, 0, 0);
		assert(handled_click);

		int start = -1, end = -1;
		tb->get_selection_range(start, end);
		assert(start == 2 && end == 2);

		// Drag inside textbox via container
		editor_event drag_ev;
		drag_ev.type = event_type::mouse_drag;
		drag_ev.mouse_x = 5;
		drag_ev.mouse_y = 0;
		bool handled_drag = container.handle_event(drag_ev, 0, 0);
		assert(handled_drag);

		tb->get_selection_range(start, end);
		assert(start == 2 && end == 5);

		// Release inside textbox via container
		editor_event release_ev;
		release_ev.type = event_type::mouse_release;
		release_ev.mouse_x = 5;
		release_ev.mouse_y = 0;
		bool handled_release = container.handle_event(release_ev, 0, 0);
		assert(handled_release);

		// Test that focus loss clears the selection
		tb->get_selection_range(start, end);
		assert(start == 2 && end == 5);
		container.set_focus(false);
		tb->get_selection_range(start, end);
		assert(start == -1 && end == -1);
	}

	// Test history traversal and cloning edits
	{
		input_history_manager::get_instance().add_entry("test_history", "command A");
		input_history_manager::get_instance().add_entry("test_history", "command B");

		ui_textbox tb("my_textbox", 40, "");
		tb.set_history_enabled(true, "test_history");
		tb.set_focus(true);

		// Type "my draft"
		editor_event ev_char;
		ev_char.type = event_type::key_press;
		for (char c : std::string("my draft")) {
			ev_char.key_code = c;
			tb.handle_event(ev_char, 0, 0);
		}
		assert(*tb.get_value("my_textbox") == "my draft");

		// Press Up
		editor_event ev_up;
		ev_up.type = event_type::key_press;
		ev_up.key_code = KEY_UP;
		tb.handle_event(ev_up, 0, 0);
		assert(*tb.get_value("my_textbox") == "command B");

		// Edit it: append 'x'
		ev_char.key_code = 'x';
		tb.handle_event(ev_char, 0, 0);
		assert(*tb.get_value("my_textbox") == "command Bx");

		// Press Up again
		tb.handle_event(ev_up, 0, 0);
		assert(*tb.get_value("my_textbox") == "command A");

		// Press Down
		editor_event ev_down;
		ev_down.type = event_type::key_press;
		ev_down.key_code = KEY_DOWN;
		tb.handle_event(ev_down, 0, 0);
		// Should return to the edited clone
		assert(*tb.get_value("my_textbox") == "command Bx");

		// Press Down again
		tb.handle_event(ev_down, 0, 0);
		// Should return to "my draft"
		assert(*tb.get_value("my_textbox") == "my draft");

		// Lose focus
		tb.set_focus(false);
		tb.set_focus(true);
		// Press Up
		tb.handle_event(ev_up, 0, 0);
		// Should start fresh and show the actual history item "command B" without modifications
		assert(*tb.get_value("my_textbox") == "command B");
	}

	std::cout << "ui_listbox and ui_element unit tests passed!\n";
	return 0;
}
