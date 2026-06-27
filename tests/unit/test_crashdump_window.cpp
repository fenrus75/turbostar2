#include <cassert>
#include <iostream>
#include <memory>
#include <fstream>
#include <filesystem>
#include "test_watchdog.h"
#include "ui/crashdump_window.h"
#include "event_queue.h"
#include "project_manager.h"
#include "fs_utils.h"
#include "markdown_utils.h"

int main()
{
	test_watchdog::setup_watchdog(10);

	project_manager::get_instance().initialize();
	crashdump_manager::get_instance().clear_all();

	// Verify markdown table alignment utility directly
	std::string unaligned = "| Name | Age |\n|---|---|\n| Alice | 30 |\n";
	std::string aligned = markdown_utils::align_all_tables(unaligned, false);
	assert(aligned.find("Alice | 30  |") != std::string::npos);

	// Setup a mock crashdump with an unaligned markdown table
	std::string dump_dir = fs_utils::get_project_dump_dir();
	std::filesystem::path crash_path = std::filesystem::path(dump_dir) / "crash_test999";
	std::filesystem::create_directories(crash_path);

	{
		std::ofstream ofs(crash_path / "info.txt");
		ofs << "Signal: 11\n";
	}
	{
		std::ofstream ofs(crash_path / "report.md");
		ofs << "| Name | Age |\n|---|---|\n| Alice | 30 |\n";
	}

	crashdump_manager::get_instance().refresh("dummy_hash");

	event_queue global_queue;
	crashdump_window win(1, 0, 0, 80, 24, global_queue);

	// Verify basic window properties
	assert(win.get_id() == 1);
	assert(win.get_width() == 80);
	assert(win.get_height() == 24);
	assert(win.get_background_color_pair() == 3);

	// Trigger draw_content to verify drawing and table alignment calculations run without crashing
	win.draw_content(false);

	// Clean up
	crashdump_manager::get_instance().clear_all();

	std::cout << "test_crashdump_window passed!" << std::endl;
	return 0;
}
