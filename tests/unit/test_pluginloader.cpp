#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "pluginloader.h"
#include "agentlib/tool_registry.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	// Force initialization of tool_registry before plugin_loader
	(void)agentlib::tool_registry::get_instance();

	auto &loader = plugin_loader::get_instance();
	loader.load_all_plugins();

	const auto &plugins = loader.get_plugins();
	(void)plugins;

	std::cout << "pluginloader unit tests passed!\n";
	return 0;
}
