#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/components/ui_listbox.h"

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

	std::cout << "ui_listbox unit tests passed!\n";
	return 0;
}
