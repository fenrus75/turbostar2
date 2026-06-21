#include <cassert>
#include <iostream>
#include <memory>
#include "test_watchdog.h"
#include "ui/agent_center_window.h"

int main()
{
	test_watchdog::setup_watchdog(10);

	// Instantiate the agent center window with a nullptr editor to test its basic TUI behavior
	agent_center_window win(1, 0, 0, 80, 24, nullptr);

	// Verify basic window properties
	assert(win.get_id() == 1);
	assert(win.get_width() == 80);
	assert(win.get_height() == 24);
	assert(win.get_displayed_title() == "Agent Command Center");

	// Trigger draw_content to verify layout calculations run without crashing
	win.draw_content(false);

	// Trigger process_events to verify basic event processing runs without crashing
	win.process_events();

	// Test Enter/Space/Carriage Return keys
	editor_event ev_enter;
	ev_enter.type = event_type::key_press;
	ev_enter.key_code = '\n';
	win.get_queue().push(ev_enter);

	editor_event ev_cr;
	ev_cr.type = event_type::key_press;
	ev_cr.key_code = '\r';
	win.get_queue().push(ev_cr);

	editor_event ev_space;
	ev_space.type = event_type::key_press;
	ev_space.key_code = ' ';
	win.get_queue().push(ev_space);

	assert(!win.process_events());

	// Verify cursor behavior
	win.set_cursor_position();

	std::cout << "test_agent_center_window passed!" << std::endl;
	return 0;
}
