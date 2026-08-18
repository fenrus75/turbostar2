// Tested source file: src/process_runner.cpp
#include "test_watchdog.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include "../../src/document.h"
#include "../../src/event_queue.h"
#include "../../src/process_runner.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	event_queue queue;
	auto doc = std::make_shared<document>(queue);

	// Test 1: Execute simple echo command
	{
		process_runner runner(doc);
		runner.set_auto_scroll(true);
		runner.execute("echo 'Hello Process Runner'");

		// Wait for execution to finish
		while (runner.is_running()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		assert(!runner.is_running());
		assert(doc->line_count() >= 4);

		bool found_hello = false;
		for (int i = 0; i < doc->line_count(); ++i) {
			if (doc->get_line(i)->get_text().find("Hello Process Runner") != std::string::npos) {
				found_hello = true;
				break;
			}
		}
		assert(found_hello);
	}

	// Test 2: Stop execution manually
	{
		process_runner runner(doc);
		runner.execute("sleep 5");
		assert(runner.is_running());

		runner.stop();
		assert(!runner.is_running());
	}

	std::cout << "process_runner unit test passed!\n";
	return 0;
}
