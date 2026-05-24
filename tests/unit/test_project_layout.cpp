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

	std::cout << "Project layout test passed!" << std::endl;
	return 0;
}
