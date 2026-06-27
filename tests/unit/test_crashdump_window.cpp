#include <cassert>
#include <iostream>
#include <memory>
#include "test_watchdog.h"
#include "ui/crashdump_window.h"
#include "event_queue.h"

int main()
{
	test_watchdog::setup_watchdog(10);

	event_queue global_queue;
	crashdump_window win(1, 0, 0, 80, 24, global_queue);

	// Verify basic window properties
	assert(win.get_id() == 1);
	assert(win.get_width() == 80);
	assert(win.get_height() == 24);

	// The background color pair MUST be 3 (yellow-on-blue)
	assert(win.get_background_color_pair() == 3);

	std::cout << "test_crashdump_window passed!" << std::endl;
	return 0;
}
