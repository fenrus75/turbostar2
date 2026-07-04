#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "pluginloader.h"
#include "agentlib/tool_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/command_registry.h"
#include <algorithm>

int main()
{
	test_watchdog::setup_watchdog(30);

	// Force initialization of tool_registry, command_registry, and skill_manager before plugin_loader
	(void)agentlib::tool_registry::get_instance();
	(void)command_registry::get_instance();
	(void)agentlib::skill_manager::get_instance();

	auto &loader = plugin_loader::get_instance();
	loader.load_all_plugins();

	const auto &plugins = loader.get_plugins();
	(void)plugins;

	// Verify that the mattpocock plugin loaded and registered its components
	auto cmd = command_registry::get_instance().get_command("grill-me");
	assert(cmd != nullptr);
	assert(cmd->get_name() == "grill-me");

	const auto &skills = agentlib::skill_manager::get_instance().get_skills();
	bool found_grill_me = false;
	for (const auto &s : skills) {
		if (s.name == "grill-me") {
			found_grill_me = true;
			assert(s.visible == false);
			assert(s.uri == "skills://grill-me/");
		}
	}
	assert(found_grill_me);

	auto vfs = agentlib::skill_manager::get_instance().get_vfs();
	auto file = vfs->read_file("skills://grill-me/SKILL.md");
	assert(file.has_value());
	assert(file.value()->view().find("grill-me") != std::string_view::npos);

	std::cout << "pluginloader unit tests passed!\n";
	return 0;
}
