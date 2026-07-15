#include <cassert>
#include <iostream>
#include <memory>
#include "ui/components/ui_button.h"
#include "ui/components/ui_textbox.h"
#include "ui/ui_element.h"
#include "event_queue.h"

int main()
{
	std::cout << "Running test_button_hotkeys..." << std::endl;

	// Create a parent container
	auto container = std::make_unique<ui_container>("container", 0, 0, 50, 20);

	// Create a textbox and a button
	auto textbox = std::make_unique<ui_textbox>("textbox", 2, 2, 20, "");
	auto* textbox_raw = textbox.get();

	bool button_clicked = false;
	auto button = std::make_unique<ui_button>("button", 2, 5, "OK", 'o', [&button_clicked]() {
		button_clicked = true;
	});
	auto* button_raw = button.get();

	container->add_child(std::move(textbox));
	container->add_child(std::move(button));

	// Scenario 1: Focus is on the textbox.
	// Typing 'o' (ASCII 111) should be consumed by the textbox and NOT trigger the button.
	container->set_focused_child(textbox_raw);
	textbox_raw->set_focus(true);
	button_raw->set_focus(false);
	button_clicked = false;

	editor_event ev;
	ev.type = event_type::key_press;
	ev.key_code = 'o';

	bool handled = container->handle_event(ev, 0, 0);
	assert(handled == true);
	assert(button_clicked == false);
	assert(textbox_raw->get_value("textbox").value_or("") == "o");

	// Scenario 2: Focus is NOT on the textbox (e.g. focus is on the button itself or nothing).
	// Typing 'o' should trigger the button.
	textbox_raw->set_focus(false);
	container->set_focused_child(nullptr);
	button_clicked = false;

	handled = container->handle_event(ev, 0, 0);
	assert(handled == true);
	assert(button_clicked == true);

	// Scenario 3: Focus is back on the textbox.
	// Typing Alt+o (sent as negative keycode, e.g. -'o' = -111) should bypass the textbox and trigger the button.
	container->set_focused_child(textbox_raw);
	textbox_raw->set_focus(true);
	button_clicked = false;
	ev.key_code = -'o';

	handled = container->handle_event(ev, 0, 0);
	assert(handled == true);
	assert(button_clicked == true);

	std::cout << "All test_button_hotkeys tests passed!" << std::endl;
	return 0;
}
