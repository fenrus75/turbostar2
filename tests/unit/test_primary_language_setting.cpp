#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include "../../src/config_manager.h"
#include "../../src/fs_utils.h"

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing primary_language and primary_language_version settings..." << std::endl;

	auto &cfg = config_manager::get_instance();

	// 1. Verify defaults
	assert(cfg.get_primary_language() == "C++");
	assert(cfg.get_primary_language_version() == "C++23");

	// 2. Modify settings
	cfg.set_primary_language("Python");
	cfg.set_primary_language_version("3.11+");

	assert(cfg.get_primary_language() == "Python");
	assert(cfg.get_primary_language_version() == "3.11+");

	// 3. Save to a temporary config file and reload
	fs::path temp_dir = fs::temp_directory_path() / "test_primary_lang_cfg";
	fs::create_directories(temp_dir);
	fs::path config_file = temp_dir / "config.ini";

	cfg.save_project(config_file.string());
	assert(fs::exists(config_file));

	// Reset values
	cfg.set_primary_language("C++");
	cfg.set_primary_language_version("C++23");

	// Load from file
	cfg.load_from_file(config_file.string());
	assert(cfg.get_primary_language() == "Python");
	assert(cfg.get_primary_language_version() == "3.11+");

	// Clean up
	fs::remove_all(temp_dir);

	std::cout << "test_primary_language_setting passed successfully!" << std::endl;
	return 0;
}
