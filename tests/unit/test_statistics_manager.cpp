#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include "../../src/statistics_manager.h"

namespace fs = std::filesystem;

void test_statistics_manager()
{
	auto &sm = statistics_manager::get_instance();

	// 1. Setup test environment
	std::string test_dir = "/tmp/turbostar_test_stats_dir";
	fs::create_directories(test_dir);
	setenv("HOME", test_dir.c_str(), 1);

	// Load to clear and initialize with the overridden HOME path
	sm.load();

	// 2. Test initial state
	assert(sm.get_stat("test_key1") == 0);

	// 3. Test incrementing stats
	sm.increment_stat("test_key1");
	assert(sm.get_stat("test_key1") == 1);

	sm.increment_stat("test_key1", 5);
	assert(sm.get_stat("test_key1") == 6);

	sm.increment_stat("test_key2", 10);
	assert(sm.get_stat("test_key2") == 10);

	// 4. Test retrieving all stats
	auto all_stats = sm.get_all_stats();
	assert(all_stats.size() >= 2);
	assert(all_stats["test_key1"] == 6);
	assert(all_stats["test_key2"] == 10);

	// 5. Test persistence (loading and saving)
	// Modify something and verify it saves (increment_stat calls save_unlocked)
	std::string stats_file = test_dir + "/.cache/turbostar/statistics.json";
	assert(fs::exists(stats_file));

	// Load stats back into a clean map
	// Since load() loads from the file, we can clear stats in-memory or spawn/re-load
	// Let's clear the stats map by corrupting the file or unsetting/reloading
	// For testing reload: let's verify load reads what was saved
	sm.load();
	assert(sm.get_stat("test_key1") == 6);
	assert(sm.get_stat("test_key2") == 10);

	// 6. Test environment override cleanup and fallback
	unsetenv("HOME");
	sm.load(); // Should fallback to ".cache/turbostar/statistics.json" and not crash

	// Clean up test directories
	try {
		fs::remove_all(test_dir);
		fs::remove_all(".cache"); // Cleanup fallback directory if created
	} catch (...) {
		// Ignore cleanup errors
	}

	std::cout << "statistics_manager unit tests passed!\n";
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_statistics_manager();
	return 0;
}
