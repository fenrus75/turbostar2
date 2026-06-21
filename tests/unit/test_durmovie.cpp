#include <cassert>
#include <iostream>
#include <thread>
#include "test_watchdog.h"
#include "ui/components/ui_durmovie.h"

const std::string MOCK_MOVIE_JSON = R"({
  "DurMovie": {
    "formatVersion": 7,
    "colorFormat": "16",
    "framerate": 10.0,
    "sizeX": 5,
    "sizeY": 3,
    "frames": [
      {
        "frameNumber": 1,
        "delay": 0,
        "contents": [
          "a b  ",
          " c   ",
          "  d  "
        ],
        "colorMap": [
          [[1, 6], [1, 6], [1, 6]],
          [[1, 6], [1, 6], [1, 6]],
          [[1, 6], [1, 6], [1, 6]],
          [[1, 6], [1, 6], [1, 6]],
          [[1, 6], [1, 6], [1, 6]]
        ]
      },
      {
        "frameNumber": 2,
        "delay": 0,
        "contents": [
          "x y  ",
          " z   ",
          "  w  "
        ],
        "colorMap": [
          [[6, 0], [6, 0], [6, 0]],
          [[6, 0], [6, 0], [6, 0]],
          [[6, 0], [6, 0], [6, 0]],
          [[6, 0], [6, 0], [6, 0]],
          [[6, 0], [6, 0], [6, 0]]
        ]
      }
    ]
  }
})";

void test_basic_parsing()
{
	std::cout << "Testing ui_durmovie JSON parsing..." << std::endl;
	ui_durmovie movie("test_movie", 0, 0, 10, 5);
	movie.load_json(MOCK_MOVIE_JSON);

	assert(movie.get_state() == durmovie_state::idle);
	assert(movie.get_idle_frame() == 0);
}

void test_state_transitions()
{
	std::cout << "Testing ui_durmovie state transitions..." << std::endl;
	ui_durmovie movie("test_movie", 0, 0, 10, 5, MOCK_MOVIE_JSON);

	assert(movie.get_state() == durmovie_state::idle);
	movie.set_state(durmovie_state::active);
	assert(movie.get_state() == durmovie_state::active);

	// In idle state, update_animation should return false (no redraw needed)
	movie.set_state(durmovie_state::idle);
	assert(!movie.update_animation());
}

void test_animation_timing()
{
	std::cout << "Testing ui_durmovie animation timing..." << std::endl;
	ui_durmovie movie("test_movie", 0, 0, 10, 5, MOCK_MOVIE_JSON);
	movie.set_state(durmovie_state::active);

	// The framerate is 10.0 Hz, which means 100ms per frame.
	// Immediately calling update_animation should return false since 100ms hasn't passed.
	assert(!movie.update_animation());

	// Sleep for 110ms to let the time budget expire
	std::this_thread::sleep_for(std::chrono::milliseconds(110));

	// Now update_animation should return true (advancing to frame 1)
	assert(movie.update_animation());

	// Calling it again immediately should return false
	assert(!movie.update_animation());
}

#include <fstream>
#include <sstream>

void test_real_json()
{
	std::cout << "Testing real security.json parsing..." << std::endl;
	// Try loading from standard relative paths depending on if run from root or build dir
	std::ifstream f("src/plugins/securityagent/security.json");
	if (!f.is_open()) {
		f.open("../src/plugins/securityagent/security.json");
	}
	assert(f.is_open() && "Could not open src/plugins/securityagent/security.json");

	std::stringstream ss;
	ss << f.rdbuf();
	std::string json_str = ss.str();

	ui_durmovie movie("real_movie", 0, 0, 20, 10);
	movie.load_json(json_str);

	assert(movie.get_state() == durmovie_state::idle);
	movie.set_state(durmovie_state::active);
	assert(movie.get_state() == durmovie_state::active);
	assert(!movie.update_animation()); // elapsed time hasn't passed yet
}

void test_default_movie()
{
	std::cout << "Testing default embedded movie loading..." << std::endl;
	ui_durmovie movie("default_movie", 0, 0, 10, 5);
	assert(movie.get_state() == durmovie_state::idle);
	movie.set_state(durmovie_state::active);
	assert(!movie.update_animation()); // elapsed time hasn't passed yet
}

void test_fs_compile_project_movie()
{
	std::cout << "Testing fs_compile_project embedded movie loading..." << std::endl;
	ui_durmovie movie("compile_movie", 0, 0, 10, 5);
	auto &reg = agentlib::agent_animation_registry::get_instance();
	auto anim = reg.get_animation("fs_compile_project");
	assert(anim != nullptr);
	assert(anim->framerate > 0);
	assert(anim->size_x > 0);
	assert(anim->size_y > 0);
}

void test_run_shell_command_movie()
{
	std::cout << "Testing run_shell_command embedded movie loading..." << std::endl;
	ui_durmovie movie("run_shell_command_movie", 0, 0, 10, 5);
	auto &reg = agentlib::agent_animation_registry::get_instance();
	auto anim = reg.get_animation("run_shell_command");
	assert(anim != nullptr);
	assert(anim->framerate > 0);
	assert(anim->size_x > 0);
	assert(anim->size_y > 0);
}

int main()
{
	test_watchdog::setup_watchdog(5); // 5 second timeout safety watchdog
	test_basic_parsing();
	test_state_transitions();
	test_animation_timing();
	test_real_json();
	test_default_movie();
	test_fs_compile_project_movie();
	test_run_shell_command_movie();

	std::cout << "All ui_durmovie tests passed!" << std::endl;
	return 0;
}
