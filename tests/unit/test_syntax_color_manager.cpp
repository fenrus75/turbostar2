#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include "syntax_color_manager.h"
#include "test_watchdog.h"

void test_syntax_color_manager_basic()
{
	std::cout << "Testing syntax_color_manager instantiation and defaults..." << std::endl;
	
	auto &manager = syntax_color_manager::get_instance();
	manager.initialize();

	// Verify defaults
	auto name = syntax_color_manager::get_attribute_name(syntax_attribute::keyword);
	assert(name == "Keyword");

	auto [fg, bg] = manager.get_color(syntax_attribute::keyword);
	// We want to make sure it's valid
	assert(fg > 0);
	assert(bg >= 0);
}

void test_syntax_color_manager_set_get()
{
	std::cout << "Testing syntax_color_manager set and get..." << std::endl;
	
	auto &manager = syntax_color_manager::get_instance();
	manager.set_color(syntax_attribute::comment, 2, 4);

	auto [fg, bg] = manager.get_color(syntax_attribute::comment);
	assert(fg == 2);
	assert(bg == 4);
}

void test_syntax_color_manager_save_load()
{
	std::cout << "Testing syntax_color_manager save and reload..." << std::endl;
	
	auto &manager = syntax_color_manager::get_instance();
	
	// Change keyword color
	manager.set_color(syntax_attribute::keyword, 5, 6);
	manager.save();

	// Set to something else in-memory
	manager.set_color(syntax_attribute::keyword, 1, 2);

	// Reload from file
	manager.reload();

	auto [fg, bg] = manager.get_color(syntax_attribute::keyword);
	assert(fg == 5);
	assert(bg == 6);
}

int main()
{
	test_watchdog::setup_watchdog(5);
	test_syntax_color_manager_basic();
	test_syntax_color_manager_set_get();
	test_syntax_color_manager_save_load();

	std::cout << "All syntax_color_manager tests passed!" << std::endl;
	return 0;
}
