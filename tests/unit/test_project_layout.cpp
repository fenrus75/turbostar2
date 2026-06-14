#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "../../src/project_manager.h"
#include "../../src/event_logger.h"

int main()
{
	// Ensure we are in a clean state if possible, though project_manager is a singleton
	project_manager &pm = project_manager::get_instance();
	pm.initialize();

	std::cout << "Waiting for inventory thread to complete..." << std::endl;
	
	// Wait up to 2 seconds for the inventory to finish (it has a 100ms start delay)
	bool ready = false;
	for (int i = 0; i < 20; ++i) {
		std::string markdown = pm.get_project_layout_markdown();
		if (!markdown.empty()) {
			std::cout << "Inventory complete!" << std::endl;
			std::cout << "Markdown output:" << std::endl;
			std::cout << markdown << std::endl;
			ready = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	assert(ready && "Project inventory failed to complete in time.");

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

	assert(knowledge_prompt.find("Recognized Project Dependencies (VFS Paths):") != std::string::npos);
	assert(knowledge_prompt.find("github://mirror/ncurses") != std::string::npos);

	std::cout << "Project layout and dependency tests passed!" << std::endl;
	return 0;
}
