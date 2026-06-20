#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/pluginloader.h"

int main()
{
	test_watchdog::setup_watchdog(30);

	plugin_loader loader;
	loader.load_all_plugins();

	std::cout << "pluginloader unit tests passed!\n";
	return 0;
}
