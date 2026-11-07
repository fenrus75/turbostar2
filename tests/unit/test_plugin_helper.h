#pragma once
#include "agentlib/command_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_registry.h"
#include "filter_registry.h"
#include "pluginloader.h"
#include <filesystem>
#include <cstdlib>

namespace test_watchdog {

inline void init_singletons()
{
	(void)::command_registry::get_instance();
	(void)agentlib::skill_manager::get_instance();
	(void)agentlib::filter_registry::get_instance();
	(void)agentlib::tool_registry::get_instance();
}

inline void init_plugin_environment()
{
	init_singletons();
	if (!getenv("TURBOSTAR_PLUGIN_DIR")) {
		if (std::filesystem::exists("./build/src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "./build/src/plugins", 1);
		} else if (std::filesystem::exists("./src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "./src/plugins", 1);
		} else if (std::filesystem::exists("../src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "../src/plugins", 1);
		}
	}
	plugin_loader::get_instance().load_all_plugins();
}

} // namespace test_watchdog
