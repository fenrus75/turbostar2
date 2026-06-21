#include <cassert>
#include <iostream>
#include "agentlib/agent_animation.h"
#include "test_watchdog.h"

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
      }
    ]
  }
})";

void test_registry_basic()
{
	std::cout << "Testing agent_animation_registry basic functions..." << std::endl;
	auto &reg = agentlib::agent_animation_registry::get_instance();

	// Test unregister/not found
	reg.unregister_animation("test_anim");
	auto anim = reg.get_animation("test_anim");
	assert(!anim);

	// Test registering valid JSON
	bool success = reg.register_animation_json("test_anim", MOCK_MOVIE_JSON);
	assert(success);

	anim = reg.get_animation("test_anim");
	assert(anim != nullptr);
	assert(anim->framerate == 10.0f);
	assert(anim->size_x == 5);
	assert(anim->size_y == 3);
	assert(anim->frames.size() == 1);
	assert(anim->frames[0].cells[0][0].glyph == "a");

	// Test unregistering
	reg.unregister_animation("test_anim");
	anim = reg.get_animation("test_anim");
	assert(!anim);
}

void test_global_apis()
{
	std::cout << "Testing global animation registration APIs..." << std::endl;
	register_agent_animation("global_anim", MOCK_MOVIE_JSON);

	auto &reg = agentlib::agent_animation_registry::get_instance();
	auto anim = reg.get_animation("global_anim");
	assert(anim != nullptr);
	assert(anim->framerate == 10.0f);

	unregister_agent_animation("global_anim");
	anim = reg.get_animation("global_anim");
	assert(!anim);
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_registry_basic();
	test_global_apis();

	std::cout << "All agent animation tests passed!" << std::endl;
	return 0;
}
