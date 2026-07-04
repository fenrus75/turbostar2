#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include "input_history_manager.h"
#include "project_manager.h"
#include "fs_utils.h"

int main()
{
	test_watchdog::setup_watchdog(10);
	project_manager::get_instance().initialize();

	std::string root = project_manager::get_instance().get_project_root();
	std::string wrong_file = root + "/.turbostar_input_history.json";
	if (std::filesystem::exists(wrong_file)) {
		std::filesystem::remove(wrong_file);
	}

	std::string cache_root = fs_utils::get_project_cache_root();
	std::string correct_file = cache_root + "/.turbostar_input_history.json";
	if (std::filesystem::exists(correct_file)) {
		std::filesystem::remove(correct_file);
	}

	// Trigger a save
	input_history_manager::get_instance().add_entry("test_history", "command A");
	input_history_manager::get_instance().save();

	std::cout << "Wrong file exists: " << std::filesystem::exists(wrong_file) << "\n";
	std::cout << "Correct file exists: " << std::filesystem::exists(correct_file) << "\n";

	// This assertion will fail until the bug is fixed, as the history file
	// is currently incorrectly placed in the project root.
	assert(std::filesystem::exists(correct_file));
	assert(!std::filesystem::exists(wrong_file));

	// Clean up
	if (std::filesystem::exists(correct_file)) {
		std::filesystem::remove(correct_file);
	}

	std::cout << "Input history path test passed successfully.\n";
	return 0;
}
