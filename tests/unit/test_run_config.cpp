#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "../../src/event_queue.h"
#include "../../src/project_manager.h"
#include "../../src/ui/components/ui_dropdown.h"

// Mock ncurses constants if needed, but they are included via ncurses.h in ui_dropdown.h
#ifndef KEY_UP
#define KEY_UP 0403
#endif
#ifndef KEY_DOWN
#define KEY_DOWN 0402
#endif

void test_candidate_detection()
{
	std::cout << "Running test_candidate_detection..." << std::endl;

	std::filesystem::path orig_path = std::filesystem::current_path();
	std::filesystem::path temp_dir = orig_path / "test_temp_meson";
	std::filesystem::create_directories(temp_dir);

	std::filesystem::path meson_file = temp_dir / "meson.build";
	std::ofstream out(meson_file);
	assert(out.is_open());
	out << "# Test meson build file\n"
	    << "project('my_test_project', 'cpp')\n"
	    << "executable('candidate_a', 'a.cpp')\n"
	    << "executable('my_test_project', 'main.cpp')\n"
	    << "executable('candidate_b', 'b.cpp')\n";
	out.close();

	// Change working directory to temp_dir
	std::filesystem::current_path(temp_dir);

	// Initialize a temporary git repo so git rev-parse returns this temp_dir
	int res = std::system("git init >/dev/null 2>&1");
	(void)res;

	project_manager &pm = project_manager::get_instance();
	std::vector<std::string> candidates = pm.detect_executable_candidates();

	// Restore original working directory
	std::filesystem::current_path(orig_path);
	std::filesystem::remove_all(temp_dir);

	assert(candidates.size() == 3);
	// my_test_project should be prioritized at the top because it matches the project name
	assert(candidates[0] == "my_test_project");
	assert(candidates[1] == "candidate_a");
	assert(candidates[2] == "candidate_b");

	std::cout << "test_candidate_detection passed!" << std::endl;
}

void test_dropdown_widget()
{
	std::cout << "Running test_dropdown_widget..." << std::endl;

	std::vector<std::string> candidates = {"initial", "second", "third"};
	ui_dropdown dd("test_dd", 0, 0, 30, "initial", candidates);

	assert(dd.name() == "test_dd");
	auto val = dd.get_value("test_dd");
	assert(val && *val == "initial");
	assert(!dd.has_overlay());

	// Set focus
	dd.set_focus(true);

	// Press KEY_DOWN to open the dropdown list
	editor_event ev_down;
	ev_down.type = event_type::key_press;
	ev_down.key_code = KEY_DOWN;
	bool handled = dd.handle_event(ev_down, 0, 0);
	assert(handled);
	assert(dd.has_overlay());

	// Press KEY_DOWN again to select the second candidate (index 1)
	handled = dd.handle_event(ev_down, 0, 0);
	assert(handled);
	assert(dd.has_overlay());

	// Press Enter to commit selection
	editor_event ev_enter;
	ev_enter.type = event_type::key_press;
	ev_enter.key_code = '\n';
	handled = dd.handle_event(ev_enter, 0, 0);
	assert(handled);
	assert(!dd.has_overlay());

	val = dd.get_value("test_dd");
	assert(val && *val == "second");

	// Type character 'x'
	editor_event ev_char;
	ev_char.type = event_type::key_press;
	ev_char.key_code = 'x';
	handled = dd.handle_event(ev_char, 0, 0);
	assert(handled);
	val = dd.get_value("test_dd");
	assert(val && *val == "secondx");

	// Reset buffer
	dd.set_buffer("initial");
	val = dd.get_value("test_dd");
	assert(val && *val == "initial");

	// Click on textbox at (10, 0) to open dropdown
	editor_event ev_click;
	ev_click.type = event_type::mouse_click;
	ev_click.mouse_x = 10;
	ev_click.mouse_y = 0;
	assert(dd.contains_coordinate(10, 0, 0, 0));
	handled = dd.handle_event(ev_click, 0, 0);
	assert(handled);
	assert(dd.has_overlay());

	// Click on second option ("second") at (5, 3)
	ev_click.mouse_x = 5;
	ev_click.mouse_y = 3;
	assert(dd.contains_coordinate(5, 3, 0, 0));
	handled = dd.handle_event(ev_click, 0, 0);
	assert(handled);
	assert(!dd.has_overlay());

	val = dd.get_value("test_dd");
	assert(val && *val == "second");

	std::cout << "test_dropdown_widget passed!" << std::endl;
}

int main()
{
	test_candidate_detection();
	test_dropdown_widget();
	std::cout << "All run config tests passed!" << std::endl;
	return 0;
}
