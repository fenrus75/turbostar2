// Tested source file: src/project_manager.cpp, src/tools/fs_run_tests/fs_run_tests_entry.cpp
//
// Unit tests for project_manager's available-test list caching/invalidation
// and fs_run_tests tokenized fuzzy test resolution.
#include "test_watchdog.h"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "config_manager.h"
#include "project_manager.h"
#include "tools/fs_run_tests/fs_run_tests.h"

int main()
{
	test_watchdog::setup_watchdog(60);
	project_manager &pm = project_manager::get_instance();
	pm.initialize();

	std::cout << "Fetching available tests (first populate)..." << std::endl;
	std::vector<std::string> first = pm.get_available_tests();
	std::cout << "First fetch returned " << first.size() << " tests." << std::endl;

	// Sanity: the meson build has real tests (the suite itself), so the list should
	// not be empty in this repo's build directory.
	assert(!first.empty() && "Expected a non-empty available test list from meson.");

	// Verify the cache returns the same list on a second, back-to-back call WITHOUT
	// invalidating (cache effectiveness: no spurious refresh between rapid lookups).
	std::vector<std::string> second = pm.get_available_tests();
	assert(second == first && "Back-to-back cached lookups should agree (cache effectiveness).");

	// Invalidate the cache and force a refresh: this is the path fs_run_tests uses when
	// a requested name misses. The re-fetched list must still contain the same tests,
	// proving the refresh path repopulates correctly (no empty/stale result).
	pm.invalidate_available_tests_cache();
	std::vector<std::string> refreshed = pm.get_available_tests();
	assert(!refreshed.empty() && "Refresh after invalidation must repopulate the list.");
	assert(refreshed == first && "Refresh after invalidation should yield the same test set.");

	// Confirm we can observe the exact names of the newest registered agent tests. These
	// were added to meson.build and built; they must be discoverable after a refresh.
	auto has_test = [&refreshed](const std::string &name) {
		return std::find(refreshed.begin(), refreshed.end(), name) != refreshed.end();
	};
	// The names here mirror the meson.build registrations; if meson sees them, the
	// refreshed cache must surface them too (this is the regression the fix addresses).
	bool saw_llm = has_test("turbostar:unit_test_llm_client");
	bool saw_state = has_test("turbostar:unit_test_ai_agent_state");
	bool saw_replay = has_test("turbostar:unit_test_record_replay_transports");
	std::cout << "llm_client test present: " << (saw_llm ? "yes" : "no") << std::endl;
	std::cout << "ai_agent_state test present: " << (saw_state ? "yes" : "no") << std::endl;
	std::cout << "record_replay test present: " << (saw_replay ? "yes" : "no") << std::endl;
	// At least one of the newest tests must be visible post-refresh. We require
	// llm_client specifically since it is the canonical new test from this effort.
	assert(saw_llm && "Refreshed available-test list must include newly-registered unit_test_llm_client.");

	std::cout << "Available-tests cache/invalidation tests passed!" << std::endl;

	std::cout << "\nTesting tokenized fuzzy test resolution (fs_run_tests)..." << std::endl;
	const std::vector<std::string> mock_available = {
		"turbostar:unit_utf8",
		"turbostar:unit_test_run_shell_command",
		"turbostar:unit_foo_alpha",
		"turbostar:unit_foo_beta",
		"turbostar:integration_git_status"
	};

	// 1. Exact match
	{
		std::vector<std::string> query = {"turbostar:unit_utf8"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.size() == 1);
		assert(res.resolved_names[0] == "turbostar:unit_utf8");
		assert(res.auto_matched_notes.empty());
		assert(res.suggestions.empty());
		assert(!res.did_substring_expand);
	}

	// 2. Substring match
	{
		std::vector<std::string> query = {"unit_utf8"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.size() == 1);
		assert(res.resolved_names[0] == "turbostar:unit_utf8");
		assert(res.auto_matched_notes.empty());
		assert(res.suggestions.empty());
	}

	// 3. Tokenized matching with noise word ("test") removal (the classic mismatch: query "unit_test_utf8" vs target "turbostar:unit_utf8")
	{
		std::vector<std::string> query = {"unit_test_utf8"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.size() == 1);
		assert(res.resolved_names[0] == "turbostar:unit_utf8");
		assert(res.auto_matched_notes.size() == 1);
		assert(res.auto_matched_notes[0].find("unit_test_utf8") != std::string::npos);
		assert(res.auto_matched_notes[0].find("turbostar:unit_utf8") != std::string::npos);
		assert(res.suggestions.empty());
	}

	// 4. Tokenized matching with inverted or colon tokens
	{
		std::vector<std::string> query = {"utf8_unit"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.size() == 1);
		assert(res.resolved_names[0] == "turbostar:unit_utf8");
		assert(!res.auto_matched_notes.empty());
	}

	// 5. Multi-candidate match yielding suggestions (2 to 5 candidates)
	{
		std::vector<std::string> query = {"unit_test_foo"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.empty());
		assert(res.suggestions.size() == 2);
		assert(std::find(res.suggestions.begin(), res.suggestions.end(), "turbostar:unit_foo_alpha") != res.suggestions.end());
		assert(std::find(res.suggestions.begin(), res.suggestions.end(), "turbostar:unit_foo_beta") != res.suggestions.end());
		assert(res.unresolved_queries.size() == 1);
		assert(res.unresolved_queries[0] == "unit_test_foo");
	}

	// 6. Completely unknown test
	{
		std::vector<std::string> query = {"nonexistent_xyz"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, mock_available);
		assert(res.resolved_names.empty());
		assert(res.suggestions.empty());
		assert(res.unresolved_queries.size() == 1);
		assert(res.unresolved_queries[0] == "nonexistent_xyz");
	}

	std::cout << "\nTesting CMake / CTest test list parser..." << std::endl;
	{
		std::string sample_ctest_output =
			"Test project /home/arjan/git/fmt/build\n"
			"  Test  #1: assert-test\n"
			"  Test  #2: chrono-test\n"
			"  Test  #3: color-test\n"
			"  Test  #4: core-test\n"
			"  Test  #5: grisu-test\n"
			"  Test  #6: gtest-extra-test\n"
			"  Test  #7: format-test\n"
			"  Test  #8: format-impl-test\n"
			"  Test  #9: locale-test\n"
			"  Test #10: ostream-test\n"
			"  Test #11: compile-test\n"
			"  Test #12: printf-test\n"
			"  Test #13: custom-formatter-test\n"
			"  Test #14: ranges-test\n"
			"  Test #15: scan-test\n"
			"  Test #16: posix-mock-test\n"
			"  Test #17: os-test\n"
			"\n"
			"Total Tests: 17\n";

		std::vector<std::string> parsed = project_manager::parse_ctest_test_list(sample_ctest_output);
		assert(parsed.size() == 17);
		assert(parsed[0] == "assert-test");
		assert(parsed[1] == "chrono-test");
		assert(parsed[2] == "color-test");
		assert(parsed[3] == "core-test");
		assert(parsed[4] == "grisu-test");
		assert(parsed[5] == "gtest-extra-test");
		assert(parsed[6] == "format-test");
		assert(parsed[7] == "format-impl-test");
		assert(parsed[8] == "locale-test");
		assert(parsed[9] == "ostream-test");
		assert(parsed[10] == "compile-test");
		assert(parsed[11] == "printf-test");
		assert(parsed[12] == "custom-formatter-test");
		assert(parsed[13] == "ranges-test");
		assert(parsed[14] == "scan-test");
		assert(parsed[15] == "posix-mock-test");
		assert(parsed[16] == "os-test");

		// Test empty and malformed outputs
		assert(project_manager::parse_ctest_test_list("").empty());
		assert(project_manager::parse_ctest_test_list("Test project /foo/bar\nTotal Tests: 0\n").empty());
		assert(project_manager::parse_ctest_test_list("Random text without test definitions").empty());

		// Test resolution with parsed CTest test names
		std::vector<std::string> query = {"format"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(query, parsed);
		// "format" matches format-test, format-impl-test, custom-formatter-test
		assert(!res.suggestions.empty() || res.resolved_names.size() >= 1);
		std::cout << "CTest test list parsing verified successfully!" << std::endl;
	}

	// Live CMake project test discovery on /home/arjan/git/fmt if present
	if (std::filesystem::exists("/home/arjan/git/fmt/build/CTestTestfile.cmake")) {
		std::cout << "\nTesting live CMake test discovery with /home/arjan/git/fmt..." << std::endl;
		std::string orig_root = pm.get_project_root();
		std::string orig_bs = config_manager::get_instance().get_build_system();
		std::string orig_bd = config_manager::get_instance().get_build_directory();

		pm.set_project_root("/home/arjan/git/fmt");
		config_manager::get_instance().set_build_system("cmake", true);
		config_manager::get_instance().set_build_directory("build");

		std::vector<std::string> fmt_tests = pm.get_available_tests();
		std::cout << "Discovered " << fmt_tests.size() << " tests in fmt project." << std::endl;
		assert(fmt_tests.size() == 17);
		assert(std::find(fmt_tests.begin(), fmt_tests.end(), "assert-test") != fmt_tests.end());
		assert(std::find(fmt_tests.begin(), fmt_tests.end(), "format-test") != fmt_tests.end());
		assert(std::find(fmt_tests.begin(), fmt_tests.end(), "ranges-test") != fmt_tests.end());

		// Test exact resolution on fmt tests
		const std::vector<std::string> fmt_query = {"assert-test"};
		auto res = tools::fs_run_tests_tool::resolve_test_names_detailed(fmt_query, fmt_tests);
		assert(res.resolved_names.size() == 1);
		assert(res.resolved_names[0] == "assert-test");

		// Restore original project state
		pm.set_project_root(orig_root);
		config_manager::get_instance().set_build_system(orig_bs, false);
		config_manager::get_instance().set_build_directory(orig_bd);
		std::cout << "Live CMake test discovery verified successfully!" << std::endl;
	}

	std::cout << "All tokenized fuzzy test resolution and CMake test parsing tests passed!" << std::endl;
	return 0;
}
