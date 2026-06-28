#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <ncurses.h>
#include "ui/components/ui_color_picker.h"
#include "event_queue.h"
#include "test_watchdog.h"

void test_color_picker_basic()
{
	std::cout << "Testing ui_color_picker instantiation and default values..." << std::endl;
	ui_color_picker picker("picker", 0, 0, 14, 1, 7);

	assert(picker.name() == "picker");
	assert(picker.natural_width() == 50);
	assert(picker.natural_height() == 6);
	assert(picker.selected_fg() == 14);
	assert(picker.selected_bg() == 1);
	assert(picker.is_focusable());

	auto val = picker.get_value("picker");
	assert(val.has_value());
	assert(val.value() == "14,1");
}

void test_color_picker_events()
{
	std::cout << "Testing ui_color_picker keyboard navigation..." << std::endl;
	ui_color_picker picker("picker", 0, 0, 14, 1, 7);
	picker.set_focus(true);

	// Start in foreground row (focus_bg_row_ = false)
	editor_event ev;
	ev.type = event_type::key_press;

	// 1. Move left on Foreground
	ev.key_code = KEY_LEFT;
	assert(picker.handle_event(ev, 0, 0));
	assert(picker.selected_fg() == 13);

	// 2. Move right on Foreground
	ev.key_code = KEY_RIGHT;
	assert(picker.handle_event(ev, 0, 0));
	assert(picker.selected_fg() == 14);

	// 3. Move down to Background row
	ev.key_code = KEY_DOWN;
	assert(picker.handle_event(ev, 0, 0));
	assert(picker.selected_fg() == 14); // remains unchanged

	// 4. Move right on Background (1 + 1 = 2)
	ev.key_code = KEY_RIGHT;
	assert(picker.handle_event(ev, 0, 0));
	assert(picker.selected_bg() == 2);

	// 5. Move left on Background (2 - 1 = 1)
	ev.key_code = KEY_LEFT;
	assert(picker.handle_event(ev, 0, 0));
	assert(picker.selected_bg() == 1);

	// 6. Move up to Foreground row
	ev.key_code = KEY_UP;
	assert(picker.handle_event(ev, 0, 0));
}

void test_color_picker_mouse()
{
	std::cout << "Testing ui_color_picker mouse clicking..." << std::endl;
	ui_color_picker picker("picker", 10, 10, 5, 2, 7);

	editor_event ev;
	ev.type = event_type::mouse_click;

	// Click on Foreground color 12: abs_x is 10. Each block is 3 wide.
	// Target coordinate: x = 10 + 12 * 3 = 46. y = 10 + 1 = 11.
	ev.mouse_x = 46;
	ev.mouse_y = 11;
	assert(picker.handle_event(ev, 10, 10));
	assert(picker.selected_fg() == 12);

	// Click on Background color 5: abs_x is 10. Each block is 4 wide.
	// Target coordinate: x = 10 + 5 * 4 = 30. y = 10 + 3 = 13.
	ev.mouse_x = 30;
	ev.mouse_y = 13;
	assert(picker.handle_event(ev, 10, 10));
	assert(picker.selected_bg() == 5);
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_color_picker_basic();
	test_color_picker_events();
	test_color_picker_mouse();

	std::cout << "All ui_color_picker tests passed!" << std::endl;
	return 0;
}
