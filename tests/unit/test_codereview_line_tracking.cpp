#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "codereview_manager.h"
#include "document.h"
#include "event_queue.h"
#include "fs_utils.h"

void test_codereview_line_tracking()
{
	std::cout << "Running test_codereview_line_tracking..." << std::endl;

	// Set up temporary project directories
	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_line_track_proj";
	std::filesystem::remove_all(temp_proj);
	std::filesystem::create_directories(temp_proj);

	// Initialize git repo in the temp project directory to get valid git rev-parse output
	int res = std::system(std::format("git -C \"{}\" init >/dev/null 2>&1", temp_proj.string()).c_str());
	(void)res;
	res = std::system(
	    std::format("git -C \"{}\" commit --allow-empty -m \"Initial commit\" >/dev/null 2>&1", temp_proj.string()).c_str());
	(void)res;

	// Set override project dir so project cache goes to this temp dir
	fs_utils::set_override_project_dir(temp_proj.string());

	codereview_manager &manager = codereview_manager::get_instance();
	manager.load_project(temp_proj.string());
	manager.clear_all();

	// Create review items
	// Item 1: target at line 5
	int id1 = manager.create_code_review_item("Fix safety issue", "src/main.cpp", 5, "original line 5", "high",
						  "Vulnerability in logic", "Add boundary check");
	// Item 2: target at line 10
	int id2 = manager.create_code_review_item("Style issue", "src/main.cpp", 10, "original line 10", "low",
						  "Missing spaces", "float y = 0.0;");

	assert(id1 == 1);
	assert(id2 == 2);

	event_queue global_queue;
	document doc(global_queue, "src/main.cpp");

	// Populate document (10 lines total)
	doc.append_line("line 1");
	doc.append_line("line 2");
	doc.append_line("line 3");
	doc.append_line("line 4");
	doc.append_line("original line 5");
	doc.append_line("line 6");
	doc.append_line("line 7");
	doc.append_line("line 8");
	doc.append_line("line 9");
	doc.append_line("original line 10");

	doc.add_listener(&manager);

	// Initial checks
	std::cout << "DEBUG: doc line count: " << doc.line_count() << std::endl;
	std::cout << "DEBUG: id1 line: " << manager.get_code_review_item(id1)->line_number << std::endl;
	std::cout << "DEBUG: id2 line: " << manager.get_code_review_item(id2)->line_number << std::endl;

	assert(manager.get_code_review_item(id1)->line_number == 5);
	assert(manager.get_code_review_item(id2)->line_number == 10);
	assert(manager.get_code_review_item(id1)->state == "new");
	assert(manager.get_code_review_item(id2)->state == "new");

	// Helper for absolute cursor movement
	auto move_to_absolute = [&](int x, int y) {
		doc.move_cursor(x - doc.get_cursor_x(), y - doc.get_cursor_y());
	};

	// 1. Insert line at top (cursor at y=0, insert newline)
	move_to_absolute(0, 0); // y=0, x=0
	doc.insert_text("new line at top\n");

	std::cout << "DEBUG: after insert top, id1 line: " << manager.get_code_review_item(id1)->line_number << std::endl;
	std::cout << "DEBUG: after insert top, id2 line: " << manager.get_code_review_item(id2)->line_number << std::endl;

	// Both items should shift down by 1
	assert(manager.get_code_review_item(id1)->line_number == 6);
	assert(manager.get_code_review_item(id2)->line_number == 11);

	// 2. Delete the inserted line at the top
	move_to_absolute(0, 0);
	doc.delete_line();

	// Both items should shift back up by 1
	assert(manager.get_code_review_item(id1)->line_number == 5);
	assert(manager.get_code_review_item(id2)->line_number == 10);

	// 3. Delete target line of Item 1 (original line 5 is now at index 4)
	move_to_absolute(0, 4);
	doc.delete_line();

	// Item 1 should become "stale" because its target line was deleted
	assert(manager.get_code_review_item(id1)->state == "stale");
	// Item 2 (original line 10) was at line 10, now it should shift up to line 9
	assert(manager.get_code_review_item(id2)->line_number == 9);

	// 4. Verify save-time content verification.
	// Let's modify the line content at line 9 (which is index 8) to mismatched text.
	move_to_absolute(0, 8);
	doc.delete_line();
	// Insert text that does not match "original line 10" at all
	doc.insert_text("completely modified text\n");

	// Save document to trigger save-time verification
	std::string target_save_file = (temp_proj / "src/main.cpp").string();
	std::filesystem::create_directories(temp_proj / "src");
	bool saved = doc.save_to_file(target_save_file);
	assert(saved);

	// Item 2 should transition to "stale" because of the line mismatch
	assert(manager.get_code_review_item(id2)->state == "stale");

	// Clean up
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);

	std::cout << "test_codereview_line_tracking passed!" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_codereview_line_tracking();
	std::cout << "All code review line tracking tests passed!" << std::endl;
	return 0;
}
