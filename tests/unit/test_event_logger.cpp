// Tested source file: src/event_logger.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include "../../src/event_logger.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	auto &logger = event_logger::get_instance();
	logger.clear_all();

	logger.log("Unit test event 1");
	logger.log("Unit test event 2 - specific string");

	auto match = logger.get_latest_matching_message("specific");
	assert(match.has_value());
	assert(match->find("Unit test event 2 - specific string") != std::string::npos);
	assert(match->find("[INFO]") != std::string::npos);

	auto no_match = logger.get_latest_matching_message("nonexistent");
	assert(!no_match.has_value());

	std::string_view sv_msg = "Unit test event 3 - string_view parameter";
	logger.log(sv_msg);
	auto sv_match = logger.get_latest_matching_message(std::string_view("string_view parameter"));
	assert(sv_match.has_value());
	assert(sv_match->find("Unit test event 3 - string_view parameter") != std::string::npos);
	assert(sv_match->find("[INFO]") != std::string::npos);

	logger.log("Formatted unit test event {} - number {}", "banana", 789);
	auto format_match = logger.get_latest_matching_message("banana");
	assert(format_match.has_value());
	assert(format_match->find("Formatted unit test event banana - number 789") != std::string::npos);

	// Test dump_recent_logs_signal_safe into a pipe
	int pipefds[2];
	assert(pipe(pipefds) == 0);

	event_logger::dump_recent_logs_signal_safe(pipefds[1], 5);
	close(pipefds[1]);

	char read_buf[2048] = {0};
	ssize_t nread = read(pipefds[0], read_buf, sizeof(read_buf) - 1);
	close(pipefds[0]);

	assert(nread > 0);
	std::string dumped(read_buf, nread);
	assert(dumped.find("Recent Debug Logs:") != std::string::npos);
	assert(dumped.find("Formatted unit test event banana - number 789") != std::string::npos);

	// --- Test log levels and helper methods ---
	logger.log_debug("Debug message {}", 1);
	auto debug_match = logger.get_latest_matching_message("Debug message 1");
	assert(debug_match.has_value());
	assert(debug_match->find("[DEBUG]") != std::string::npos);

	logger.log_info("Info message {}", 2);
	auto info_match = logger.get_latest_matching_message("Info message 2");
	assert(info_match.has_value());
	assert(info_match->find("[INFO]") != std::string::npos);

	logger.log_warn("Warn message {}", 3);
	auto warn_match = logger.get_latest_matching_message("Warn message 3");
	assert(warn_match.has_value());
	assert(warn_match->find("[WARN]") != std::string::npos);

	logger.log_warning("Warning alias message {}", 4);
	auto warning_match = logger.get_latest_matching_message("Warning alias message 4");
	assert(warning_match.has_value());
	assert(warning_match->find("[WARN]") != std::string::npos);

	// Test error logging and error records tracking
	size_t errors_before = logger.get_total_error_count();
	logger.log_error("First error occurred: {}", "disk full");
	assert(logger.get_total_error_count() == errors_before + 1);

	auto err_match = logger.get_latest_matching_message("First error occurred");
	assert(err_match.has_value());
	assert(err_match->find("[ERROR]") != std::string::npos);

	logger.log_fatal("Fatal error: system out of memory");
	assert(logger.get_total_error_count() == errors_before + 2);
	auto fatal_match = logger.get_latest_matching_message("Fatal error");
	assert(fatal_match.has_value());
	assert(fatal_match->find("[FATAL]") != std::string::npos);

	// Test get_recent_errors
	auto recent = logger.get_recent_errors(5);
	assert(recent.size() >= 2);
	assert(recent.back().message == "Fatal error: system out of memory");
	assert(recent.back().level == log_level::fatal);

	// Test print_exit_error_report() with stdout redirection
	{
		std::ostringstream oss;
		auto *old_buf = std::cout.rdbuf(oss.rdbuf());
		logger.print_exit_error_report();
		std::cout.rdbuf(old_buf);

		std::string out = oss.str();
		assert(out.find("Turbostar Session Diagnostics:") != std::string::npos);
		assert(out.find("First error occurred: disk full") != std::string::npos);
		assert(out.find("Fatal error: system out of memory") != std::string::npos);
	}

	// Test min_level filtering
	logger.set_min_level(log_level::warn);
	assert(logger.get_min_level() == log_level::warn);

	logger.log_debug("Should be filtered out debug");
	assert(!logger.get_latest_matching_message("Should be filtered out debug").has_value());

	logger.log_info("Should be filtered out info");
	assert(!logger.get_latest_matching_message("Should be filtered out info").has_value());

	logger.log_warn("Should be accepted warn");
	assert(logger.get_latest_matching_message("Should be accepted warn").has_value());

	// Reset min_level to debug
	logger.set_min_level(log_level::debug);

	// Test clear_all resets errors and total error count
	logger.clear_all();
	assert(logger.get_total_error_count() == 0);
	assert(logger.get_recent_errors().empty());

	// Verify print_exit_error_report() produces nothing when 0 errors exist
	{
		std::ostringstream oss;
		auto *old_buf = std::cout.rdbuf(oss.rdbuf());
		logger.print_exit_error_report();
		std::cout.rdbuf(old_buf);

		std::string out = oss.str();
		assert(out.empty());
	}

	std::cout << "event_logger unit tests passed!\n";
	return 0;
}
