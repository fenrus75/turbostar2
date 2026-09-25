// Tested source file: src/editor_events_ui.cpp
#include <cassert>
#include <format>
#include <iostream>
#include <string>
#include <vector>
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	std::string python_bin = fs_utils::find_executable("python3");
	if (python_bin.empty()) {
		std::cout << "python3 not found on system, skipping test" << std::endl;
		return 0;
	}

	config_manager::get_instance().set_main_executable("python3");

	uint64_t start_seq = event_logger::get_instance().get_total_event_count();

	editor_options opts;
	opts.exit_immediately = 0.0;
	opts.no_welcome = true;
	editor ed(opts);

	auto res = ed.start_app("", false);
	assert(res.app_run_id > 0);

	uint64_t end_seq = event_logger::get_instance().get_total_event_count();
	std::vector<std::string> logs = event_logger::get_instance().get_event_slice(start_seq, end_seq);

	bool found_expected_cmd = false;
	std::string expected_substr = std::format("Starting app: '{}'", python_bin);
	for (const auto &line : logs) {
		if (line.find(expected_substr) != std::string::npos) {
			found_expected_cmd = true;
			break;
		}
	}

	if (res.app_run_id > 0) {
		ed.terminate_run(res.app_run_id);
	}

	assert(found_expected_cmd);

	std::cout << "test_editor_start_app passed successfully!" << std::endl;
	return 0;
}
