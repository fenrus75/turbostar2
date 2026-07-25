#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <unistd.h>
#include "../../src/event_logger.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	auto &logger = event_logger::get_instance();

	logger.log("Unit test event 1");
	logger.log("Unit test event 2 - specific string");

	auto match = logger.get_latest_matching_message("specific");
	assert(match.has_value());
	assert(match->find("Unit test event 2 - specific string") != std::string::npos);

	auto no_match = logger.get_latest_matching_message("nonexistent");
	assert(!no_match.has_value());

	logger.log("Formatted unit test event {} - number {}", "banana", 789);
	auto format_match = logger.get_latest_matching_message("banana");
	assert(format_match.has_value());
	assert(format_match->find("Formatted unit test event banana - number 789") != std::string::npos);

	// Test dump_recent_logs_signal_safe into a pipe
	int pipefds[2];
	assert(pipe(pipefds) == 0);

	event_logger::dump_recent_logs_signal_safe(pipefds[1], 5);
	close(pipefds[1]);

	char read_buf[2048] = {0};
	ssize_t nread = read(pipefds[0], read_buf, sizeof(read_buf) - 1);
	close(pipefds[0]);

	assert(nread > 0);
	std::string dumped(read_buf, nread);
	assert(dumped.find("Recent Debug Logs:") != std::string::npos);
	assert(dumped.find("Formatted unit test event banana - number 789") != std::string::npos);

	std::cout << "event_logger unit tests passed!\n";
	return 0;
}
