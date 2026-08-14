// test_crash_handler_race.cpp
//
// Unit test for dual-launch race condition on crash files (Option A implementation).

#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

#include "crash_handler.h"
#include "fs_utils.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::filesystem::path crash_dir = std::filesystem::path(cache_dir) / "crashes";

	// 1. Simulate Session 1 startup
	crash_handler::install_fallback_handler();

	// Option A requirement: On normal startup, NO 0-byte crash file should be created on disk.
	size_t count_0byte = 0;
	if (std::filesystem::exists(crash_dir)) {
		for (const auto &p : std::filesystem::directory_iterator(crash_dir)) {
			if (p.is_regular_file() && std::filesystem::file_size(p.path()) == 0) {
				count_0byte++;
			}
		}
	}
	assert(count_0byte == 0 && "Option A: No 0-byte crash file should be created on startup.");

	// 2. Simulate Session 2 startup (running concurrently)
	// Must not delete files or error out
	crash_handler::install_fallback_handler();

	// 3. Verify crash_fd is -1 initially (deferred creation)
	assert(crash_handler::crash_fd == -1 && "Crash FD must be -1 until a crash occurs.");

	std::cout << "test_crash_handler_race passed cleanly!" << std::endl;
	return 0;
}
