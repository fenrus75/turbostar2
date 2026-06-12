#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_buttons_vertical.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_dropdown.h"

int main()
{
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
	assert(btn.width() == 8);
	assert(btn.height() == 1);
	assert(btn.natural_width() == 9);
	assert(btn.natural_height() == 1);

	ui_button btn2("btn_close", 0, 0, "Close", 'c', nullptr);
	assert(btn2.width() == 5);
	assert(btn2.height() == 1);
	assert(btn2.natural_width() == 8);
	assert(btn2.natural_height() == 1);

	// Test set_position helper
	btn2.set_position(5, 12);
	assert(btn2.x() == 5);
	assert(btn2.y() == 12);
	assert(btn2.width() == 5);
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
		assert(cb.natural_width() == 15); // "Enable LSP" (10) + 5 = 15
	}

	// Test ui_dropdown focusability
	{
		ui_dropdown dd("my_dropdown", 0, 0, 20, "init", {"cand1", "cand2"});
		assert(dd.is_focusable());
	}

	std::cout << "ui_listbox and ui_element unit tests passed!\n";
	return 0;
}
