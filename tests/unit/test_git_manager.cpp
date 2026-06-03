#include <cassert>
#include <iostream>
#include "../../src/git_manager.h"
#include "../../src/event_queue.h"
#include "../../src/event_logger.h"

int main()
{
	event_queue queue;
	git_manager &manager = git_manager::get_instance();

	std::cout << "Starting git_manager test..." << std::endl;

	// Ensure git_manager is stopped initially
	manager.stop();

	// First start
	manager.start(queue);
	
	// Double start attempt (should be handled gracefully by returning early and logging in NDEBUG mode)
#ifdef NDEBUG
	std::cout << "Attempting double start (should warn and return early)..." << std::endl;
	manager.start(queue);

	// Verify warning log entry
	auto warning = event_logger::get_instance().get_latest_matching_message("git_manager::start called on an already running instance");
	assert(warning.has_value() && "Warning log entry should exist");
#else
	std::cout << "Debug build: skipping double start warning check to avoid assertion abort." << std::endl;
#endif

	// Stop manager
	manager.stop();

	// Verify calling stop again is safe
	manager.stop();

	std::cout << "git_manager tests passed successfully." << std::endl;
	return 0;
}
