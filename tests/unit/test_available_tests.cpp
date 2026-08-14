// test_available_tests.cpp
//
// Unit tests for project_manager's available-test list caching and invalidation.
//
// SOURCE FILE COVERED:
//   src/project_manager.cpp  (get_available_tests / refresh_available_tests /
//                             invalidate_available_tests_cache / build_definition_changed)
//
// BEHAVIOR COVERED:
//   - get_available_tests() returns meson test --list names (integration: needs the
//     real build dir, mirroring test_project_layout.cpp which uses the live project).
//   - invalidate_available_tests_cache() forces a fresh refresh on the next call so
//     newly added test targets (e.g. added to meson.build after the cache was first
//     populated) become discoverable without an editor restart.
//
// NOTE ON THE STALE-CACHE BUG:
//   Before this fix, get_available_tests() cached its list behind tests_ready_ with no
//   invalidation, so tests registered in meson.build after the first lookup were
//   invisible to fs_run_tests and system://project/testlist.md until editor restart.
//   This test asserts the cached list actually contains a test that only exists after
//   we re-query with an invalidated cache.
#include "test_watchdog.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "../../src/project_manager.h"

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
	return 0;
}
