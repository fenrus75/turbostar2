#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>
#include "codereview_manager.h"
#include "fs_utils.h"

void test_codereview_manager_lifecycle()
{
	std::cout << "Running test_codereview_manager_lifecycle..." << std::endl;

	// Set up temporary project directories
	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_review_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Initialize git repo in the temp project directory to get valid git rev-parse output
	int res = std::system(std::format("git -C \"{}\" init >/dev/null 2>&1", temp_proj.string()).c_str());
	(void)res;
	// Create a commit so we have a valid HEAD hash
	res = std::system(
	    std::format("git -C \"{}\" commit --allow-empty -m \"Initial commit\" >/dev/null 2>&1", temp_proj.string()).c_str());
	(void)res;

	// Set override project dir so project cache goes to this temp dir
	fs_utils::set_override_project_dir(temp_proj.string());

	codereview_manager &manager = codereview_manager::get_instance();
	manager.load_project(temp_proj.string());
	manager.clear_all();

	// Test 1: Verify empty list initially
	auto items = manager.list_code_review_items();
	assert(items.empty());

	// Test 2: Create a review item
	int id1 = manager.create_code_review_item("Fix security leak", "src/main.cpp", 42, "void login() {", "high",
						  "Potential command injection", "Use escape_shell_arg");
	assert(id1 == 1);

	// Test 3: Get item details
	auto item_opt = manager.get_code_review_item(id1);
	assert(item_opt.has_value());
	assert(item_opt->id == 1);
	assert(item_opt->summary == "Fix security leak");
	assert(item_opt->filename == "src/main.cpp");
	assert(item_opt->line_number == 42);
	assert(item_opt->line_content == "void login() {");
	assert(item_opt->state == "new");
	assert(item_opt->severity == "high");
	assert(item_opt->description == "Potential command injection");
	assert(item_opt->proposed_fix == "Use escape_shell_arg");
	assert(!item_opt->git_hash.empty());

	// Test 4: Update review item
	bool updated =
	    manager.update_code_review_item(id1, "disputed", "medium", "Refactored summary of vulnerability", "No proposed fix needed");
	assert(updated);

	item_opt = manager.get_code_review_item(id1);
	assert(item_opt->state == "disputed");
	assert(item_opt->severity == "medium");
	assert(item_opt->description == "Refactored summary of vulnerability");
	assert(item_opt->proposed_fix == "No proposed fix needed");

	// Test 5: Confirm review item
	// Revert to new first to test transition
	manager.update_code_review_item(id1, "new", std::nullopt, std::nullopt, std::nullopt);
	bool confirmed = manager.confirm_code_review_item(id1);
	assert(confirmed);
	item_opt = manager.get_code_review_item(id1);
	assert(item_opt->state == "confirmed");

	// Test 6: Resolve review item
	bool resolved = manager.resolve_code_review_item(id1, "commitabc123");
	assert(resolved);
	item_opt = manager.get_code_review_item(id1);
	assert(item_opt->state == "resolved");
	assert(item_opt->resolved_in_commit == "commitabc123");

	// Test 7: Confirm resolved item -> verified-fixed
	confirmed = manager.confirm_code_review_item(id1);
	assert(confirmed);
	item_opt = manager.get_code_review_item(id1);
	assert(item_opt->state == "verified-fixed");

	// Test 8: List items (by default resolved/verified-fixed are excluded)
	items = manager.list_code_review_items();
	assert(items.empty());

	// Listing with include_resolved = true
	items = manager.list_code_review_items("", "", true);
	assert(items.size() == 1);
	assert(items[0].id == id1);

	// Test 9: Create second item, filter list
	int id2 = manager.create_code_review_item("Style issue", "src/utils.cpp", 10, "int x=5;", "nit", "Missing spaces around operator",
						  "int x = 5;");
	assert(id2 == 2);

	// List without resolved items (should return id2 only)
	items = manager.list_code_review_items();
	assert(items.size() == 1);
	assert(items[0].id == id2);

	// List with filename filter
	items = manager.list_code_review_items("src/main.cpp", "", true);
	assert(items.size() == 1);
	assert(items[0].id == id1);

	items = manager.list_code_review_items("src/utils.cpp", "", true);
	assert(items.size() == 1);
	assert(items[0].id == id2);

	// List with severity filter
	items = manager.list_code_review_items("", "nit", true);
	assert(items.size() == 1);
	assert(items[0].id == id2);

	// Test 10: Load from disk
	// Create a new manager instance/load again to simulate restart
	manager.load_project(temp_proj.string());
	auto items_loaded = manager.list_code_review_items("", "", true);
	assert(items_loaded.size() == 2);

	// Test 11: Clear all
	manager.clear_all();
	items = manager.list_code_review_items("", "", true);
	assert(items.empty());

	// Clean up
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);

	std::cout << "test_codereview_manager_lifecycle passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_codereview_manager_lifecycle();
	std::cout << "All code review manager tests passed!" << std::endl;
	return 0;
}
