#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

#include "codereview_manager.h"
#include "event_queue.h"
#include "fs_utils.h"
#include "ui/code_review_window.h"

void test_code_review_window_behavior()
{
	std::cout << "Running test_code_review_window_behavior..." << std::endl;

	// Set up temporary project directories
	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_proj = orig_path / "test_temp_review_win_proj";
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

	// Create some review items
	int id1 = manager.create_code_review_item("Fix safety issue", "src/main.cpp", 10, "int x = 5;", "high",
						  "Vulnerability in logic", "Add boundary check");
	int id2 = manager.create_code_review_item("Style issue", "src/utils.cpp", 20, "float y=0.0;", "low",
						  "Missing spaces", "float y = 0.0;");

	assert(id1 == 1);
	assert(id2 == 2);

	event_queue global_queue;

	// Instantiate the code review window
	code_review_window win(1, 0, 0, 80, 24, global_queue);
	win.set_active(true);

	// Trigger content drawing to verify it doesn't crash without active ncurses screen
	win.draw_content(false);
	win.draw_border();

	// Test 1: Check initial selection
	// The window will populate and list the items.
	// Since listbox selection defaults to 0, item 1 (id1) is focused initially.
	// Let's verify selection through manager states after sending events.

	// Test 2: Confirm Item (Press 'c')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'c';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id1);
		assert(item.has_value());
		assert(item->state == "confirmed");

		// Global event queue should receive codereview_updated
		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id1);
	}

	// Test 3: Dispute Item (Press 'd')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'd';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id1);
		assert(item.has_value());
		assert(item->state == "disputed");

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id1);
	}

	// Test 4: Invalidate Item (Press 'i')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'i';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id1);
		assert(item.has_value());
		assert(item->state == "invalid");

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id1);
	}

	// Test 5: Resolve Item (Press 'r')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'r';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id1);
		assert(item.has_value());
		assert(item->state == "resolved");

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id1);
	}

	// Test 6: Verify Fixed Item (Press 'v')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'v';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id1);
		assert(item.has_value());
		assert(item->state == "verified-fixed");

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id1);
	}

	// Test 7: Navigation & focus_item
	// Focus on id2
	win.focus_item(id2);

	// Confirm focused item is id2 by pressing 'c' and verifying id2 is confirmed
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'c';
		win.get_window_queue().push(ev);
		win.process_events();

		auto item = manager.get_code_review_item(id2);
		assert(item.has_value());
		assert(item->state == "confirmed");

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_updated);
		assert(glob_ev->key_code == id2);
	}

	// Test 8: Comment (Press 'a')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'a';
		win.get_window_queue().push(ev);
		win.process_events();

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_action);
		assert(glob_ev->key_code == id2);
		assert(glob_ev->payload == "comment");
	}

	// Test 9: Edit (Press 'e')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'e';
		win.get_window_queue().push(ev);
		win.process_events();

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_action);
		assert(glob_ev->key_code == id2);
		assert(glob_ev->payload == "edit");
	}

	// Test 10: Mouse Click on Bottom Border Actions
	// Reset selection to id2 (index 1) and click go to source (x offsets 2-14)
	{
		editor_event ev;
		ev.type = event_type::mouse_click;
		ev.mouse_x = 5; // Within "Go to Src" area
		ev.mouse_y = 23; // Bottom border row (y_ + height_ - 1)
		win.get_window_queue().push(ev);
		win.process_events();

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::open_file);
		// Payload format should be absolute_path:line
		assert(glob_ev->payload.find("src/utils.cpp:20") != std::string::npos);
	}

	// Test 11: Refresh window
	// If database state updates externally, refresh should repopulate.
	manager.update_code_review_item(id2, "resolved", std::nullopt, std::nullopt, std::nullopt);
	win.refresh();
	// Verify detail window contains resolved state info if we draw or check
	auto item2 = manager.get_code_review_item(id2);
	assert(item2->state == "resolved");

	// Test 12: Re-process (Press 'p')
	{
		editor_event ev;
		ev.type = event_type::key_press;
		ev.key_code = 'p';
		win.get_window_queue().push(ev);
		win.process_events();

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_action);
		assert(glob_ev->key_code == id2);
		assert(glob_ev->payload == "reprocess");
	}

	// Test 13: Mouse Click on Re-process
	{
		editor_event ev;
		ev.type = event_type::mouse_click;
		ev.mouse_x = 110; // Within "Re-process" area (107-122)
		ev.mouse_y = 23; // Bottom border row
		win.get_window_queue().push(ev);
		win.process_events();

		auto glob_ev = global_queue.pop();
		assert(glob_ev.has_value());
		assert(glob_ev->type == event_type::codereview_action);
		assert(glob_ev->key_code == id2);
		assert(glob_ev->payload == "reprocess");
	}

	// Test 14: Sanitize newlines/carriage returns in line_content display
	{
		int id3 = manager.create_code_review_item("Newline issue", "src/newline.cpp", 5, "line1\nline2\rline3", "medium",
							  "Has newlines", "Fix newlines");
		win.refresh();
		win.focus_item(id3);
		// Call draw_content to verify that the code review window handles and formats the sanitization
		// of line content containing newlines and carriage returns without any issues.
		win.draw_content(false);
	}

	// Clean up
	fs_utils::set_override_project_dir("");
	std::filesystem::remove_all(temp_proj);

	std::cout << "test_code_review_window_behavior passed!" << std::endl;
}

int main()
{
	test_code_review_window_behavior();
	std::cout << "All code review window unit tests passed!" << std::endl;
	return 0;
}
