// test_debugger_detection.cpp
//
// Unit test for is_debugger_attached() in crash_handler.

#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "crash_handler.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	// Under normal test execution without GDB, is_debugger_attached() should return false (TracerPid: 0).
	bool tracer_attached = crash_handler::is_debugger_attached();
	assert(!tracer_attached && "Under normal test execution without GDB attached, is_debugger_attached() must be false.");

	std::cout << "test_debugger_detection passed cleanly! (is_debugger_attached=" << (tracer_attached ? "true" : "false") << ")" << std::endl;
	return 0;
}
