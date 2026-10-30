// Tested source file: src/project_manager.cpp
#include "test_watchdog.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "../../src/project_manager.h"
#include "../../src/event_logger.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	// Ensure we are in a clean state if possible, though project_manager is a singleton
	project_manager &pm = project_manager::get_instance();
	pm.initialize();

	// Check dependencies scanning
	auto deps = pm.get_detected_dependencies();
	auto urls = pm.get_github_vfs_urls();
	auto knowledge_prompt = pm.get_project_knowledge_prompt();

	std::cout << "Detected dependencies:" << std::endl;
	for (const auto &dep : deps) {
		std::cout << " - " << dep << std::endl;
	}

	std::cout << "GitHub VFS URLs:" << std::endl;
	for (const auto &url : urls) {
		std::cout << " - " << url << std::endl;
	}

	bool found_ncursesw = false;
	for (const auto &dep : deps) {
		if (dep == "ncursesw") {
			found_ncursesw = true;
			break;
		}
	}
	assert(found_ncursesw && "Expected ncursesw dependency to be detected");

	bool found_ncurses_url = false;
	for (const auto &url : urls) {
		if (url == "github://mirror/ncurses") {
			found_ncurses_url = true;
			break;
		}
	}
	assert(found_ncurses_url && "Expected github://mirror/ncurses URL to be resolved");

	// Check mapped dependencies pairs
	auto mapped = pm.get_mapped_dependencies();
	bool found_ncurses_mapped = false;
	for (const auto &pair : mapped) {
		if (pair.first == "ncursesw" && pair.second == "github://mirror/ncurses") {
			found_ncurses_mapped = true;
			break;
		}
	}
	assert(found_ncurses_mapped && "Expected mapped ncursesw pair to be stored");

	assert(knowledge_prompt.find("Recognized Project Dependencies (VFS Paths):") != std::string::npos);
	assert(knowledge_prompt.find("| dependency | VFS path |") != std::string::npos);
	assert(knowledge_prompt.find("| ncursesw | github://mirror/ncurses |") != std::string::npos);

	std::cout << "Project dependency tests passed!" << std::endl;
	return 0;
}
