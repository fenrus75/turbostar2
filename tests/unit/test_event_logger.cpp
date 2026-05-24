#include <cassert>
#include <iostream>
#include "../../src/event_logger.h"

int main()
{
	auto &logger = event_logger::get_instance();

	logger.log("Unit test event 1");
	logger.log("Unit test event 2 - specific string");

	auto match = logger.get_latest_matching_message("specific");
	assert(match.has_value());
	assert(*match == "Unit test event 2 - specific string");

	auto no_match = logger.get_latest_matching_message("nonexistent");
	assert(!no_match.has_value());

	std::cout << "event_logger unit tests passed!\n";
	return 0;
}
