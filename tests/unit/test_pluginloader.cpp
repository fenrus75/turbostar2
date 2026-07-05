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

	// Verify that the image_basic plugin loaded and registered its components if available
	bool found_image_basic = false;
	for (const auto &p : loader.get_plugins()) {
		if (std::string(p.name) == "Basic Image Operations") {
			found_image_basic = true;
		}
	}
	if (found_image_basic) {
		auto &registry = agentlib::tool_registry::get_instance();
		agentlib::agent_properties props;
		props.active_families.push_back("image");
		auto validators = registry.get_active_tools(true, props);
		bool has_image_resize = std::any_of(validators.begin(), validators.end(), [](const auto &v) {
			return v->get_name() == "image_resize";
		});
		bool has_image_crop = std::any_of(validators.begin(), validators.end(), [](const auto &v) {
			return v->get_name() == "image_crop";
		});
		bool has_image_rotate = std::any_of(validators.begin(), validators.end(), [](const auto &v) {
			return v->get_name() == "image_rotate";
		});
		assert(has_image_resize);
		assert(has_image_crop);
		assert(has_image_rotate);
		assert(registry.has_tool_family("image"));
	}

	std::cout << "pluginloader unit tests passed!\n";
	return 0;
}
