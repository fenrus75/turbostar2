#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <fstream>
#include "ui/components/ui_thumbnail.h"
#include "filter_registry.h"
#include "test_watchdog.h"
#include <nlohmann/json.hpp>

void write_dummy_file(const std::string &path)
{
	std::ofstream out(path);
	out << "dummy image data";
}

void test_thumbnail_basic()
{
	std::cout << "Testing ui_thumbnail instantiation..." << std::endl;
	ui_thumbnail thumb("my_thumb", 5, 10, 20, 10);

	assert(thumb.name() == "my_thumb");
	assert(thumb.x() == 5);
	assert(thumb.y() == 10);
	assert(thumb.width() == 20);
	assert(thumb.height() == 10);
}

void test_thumbnail_filter_binding()
{
	std::cout << "Testing ui_thumbnail filter invocation and parsing..." << std::endl;
	
	std::string dummy_path = "./dummy_test_image.png";
	write_dummy_file(dummy_path);

	agentlib::filter_registry::get_instance().register_filter("image_thumbnail", [](const std::string &input_json) {
		auto in = nlohmann::json::parse(input_json);
		assert(in.contains("path"));
		assert(in.contains("width"));
		assert(in.contains("height"));

		nlohmann::json out = {
			{"format", "quad_v1"},
			{"width", 2},
			{"height", 2},
			{"cells", {
				{{255, 0, 0}, {0, 255, 0}, "▀"},
				{{0, 0, 255}, {255, 255, 255}, "▄"},
				{{10, 20, 30}, {40, 50, 60}, "▌"},
				{{70, 80, 90}, {100, 110, 120}, "▚"}
			}}
		};
		return out.dump();
	});

	ui_thumbnail thumb("my_thumb", 0, 0, 2, 2);
	thumb.set_image_path(dummy_path);

	thumb.clear_image();
	
	std::filesystem::remove(dummy_path);
	agentlib::filter_registry::get_instance().unregister_filter("image_thumbnail");
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_thumbnail_basic();
	test_thumbnail_filter_binding();

	std::cout << "All ui_thumbnail tests passed!" << std::endl;
	return 0;
}
