#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "session_manager.h"
#include "fs_utils.h"

void test_session_persistence()
{
	std::cout << "Running test_session_persistence..." << std::endl;

	// Isolate HOME directory for global fallback config
	const char *orig_home = getenv("HOME");
	std::string orig_home_str = orig_home ? orig_home : "";

	std::filesystem::path temp_home = std::filesystem::current_path() / "test_session_temp_home";
	std::filesystem::path temp_proj = std::filesystem::current_path() / "test_session_temp_proj";

	std::filesystem::remove_all(temp_home);
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_home);
	std::filesystem::create_directories(temp_proj);

	setenv("HOME", temp_home.string().c_str(), 1);

	// 1. Test Project-Specific Session Persistence
	fs_utils::set_override_project_dir(temp_proj.string());

	session_manager &mgr = session_manager::get_instance();
	mgr.set_last_search_query("find_this");
	mgr.set_last_replace_query("replace_with_this");
	mgr.save();

	// Verify project session.json file is created
	std::filesystem::path proj_session = std::filesystem::path(fs_utils::get_project_cache_root()) / "session.json";
	assert(std::filesystem::exists(proj_session));

	// Clear memory state
	mgr.set_last_search_query("");
	mgr.set_last_replace_query("");

	// Load and assert
	mgr.load();
	assert(mgr.get_last_search_query() == "find_this");
	assert(mgr.get_last_replace_query() == "replace_with_this");

	// 2. Test Global Session Persistence fallback (when not in project cache root)
	fs_utils::set_override_project_dir(""); // Clear project root
	mgr.set_last_search_query("global_find");
	mgr.set_last_replace_query("global_replace");
	mgr.save();

	// Verify global session.json is created
	std::filesystem::path global_session = temp_home / ".turbostar" / "session.json";
	assert(std::filesystem::exists(global_session));

	// Clear memory
	mgr.set_last_search_query("");
	mgr.set_last_replace_query("");

	// Load and assert
	mgr.load();
	assert(mgr.get_last_search_query() == "global_find");
	assert(mgr.get_last_replace_query() == "global_replace");

	// Clean up
	if (!orig_home_str.empty()) {
		setenv("HOME", orig_home_str.c_str(), 1);
	} else {
		unsetenv("HOME");
	}
	std::filesystem::remove_all(temp_home);
	std::filesystem::remove_all(temp_proj);

	std::cout << "test_session_persistence passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_session_persistence();
	std::cout << "All session manager tests passed!" << std::endl;
	return 0;
}
