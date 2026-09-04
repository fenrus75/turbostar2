// Tested source file: src/project_manager.cpp, src/tools/fs_run_tests/fs_run_tests_entry.cpp
//
// Unit tests for project_manager's available-test list caching/invalidation
// and fs_run_tests tokenized fuzzy test resolution.
#include "test_watchdog.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

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

	std::cout << "All tokenized fuzzy test resolution tests passed!" << std::endl;
	return 0;
}
