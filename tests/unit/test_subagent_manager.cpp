#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/agentlib/agent_animation.h"
#include "../../src/event_logger.h"

using namespace agentlib;

void write_file(const std::filesystem::path& path, const std::string& content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

void test_subagent_manager_basic()
{
	// Setup test home directory
	std::filesystem::path temp_home = std::filesystem::absolute("./test_subagents_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);

	// Set HOME environment variable
	setenv("HOME", temp_home.c_str(), 1);

	// 1. Verify builtins are loaded
	auto& manager = subagent_manager::get_instance();
	manager.initialize();

	const auto& subagents = manager.get_subagents();
	assert(subagents.size() >= 2);

	auto research_opt = manager.find_subagent_by_name("research");
	assert(research_opt.has_value());
	assert(research_opt->read_only == true);
	assert(research_opt->tool_families.size() == 1);
	assert(research_opt->tool_families[0] == "base");

	auto self_opt = manager.find_subagent_by_name("self");
	assert(self_opt.has_value());
	assert(self_opt->read_only == false);
	assert(self_opt->tool_families.size() == 3);

	// 2. Create custom global agent file to scan (verifying camelCase and snake_case properties)
	std::filesystem::path agents_dir = temp_home / ".agents";
	std::filesystem::create_directories(agents_dir);

	std::string custom_agent_md = 
		"---\n"
		"name: custom-agent\n"
		"description: A test subagent\n"
		"model: test-model-pro\n"
		"tools:\n"
		"  - fs_read\n"
		"  - ask_user\n"
		"toolFamilies:\n"
		"  - base\n"
		"  - x86\n"
		"readOnly: true\n"
		"maxTurns: 15\n"
		"animation: custom_agent.json\n"
		"---\n"
		"This is the custom subagent system prompt.\n";

	write_file(agents_dir / "custom_agent.md", custom_agent_md);
	write_file(agents_dir / "custom_agent.json", "{\"DurMovie\": {\"version\": 1, \"frames\": []}}");

	// Override research builtin in local/global path
	std::string override_research_md = 
		"---\n"
		"name: research\n"
		"description: Overridden research description\n"
		"read_only: false\n"
		"---\n"
		"Overridden system prompt.\n";

	write_file(agents_dir / "research.md", override_research_md);

	// Reinitialize and verify
	manager.initialize();

	auto custom_opt = manager.find_subagent_by_name("custom-agent");
	assert(custom_opt.has_value());
	assert(custom_opt->description == "A test subagent");
	assert(custom_opt->model.value() == "test-model-pro");
	assert(custom_opt->tools.size() == 2);
	assert(custom_opt->tools[0] == "fs_read");
	assert(custom_opt->tool_families.size() == 2);
	assert(custom_opt->tool_families[1] == "x86");
	assert(custom_opt->read_only == true);
	assert(custom_opt->max_turns.value() == 15);
	assert(custom_opt->system_prompt == "This is the custom subagent system prompt.");
	assert(custom_opt->animation_path == "custom_agent.json");
	assert(custom_opt->animation_name == "custom-agent");

	auto custom_anim = agent_animation_registry::get_instance().get_animation("custom-agent");
	assert(custom_anim != nullptr);

	// Verify override precedence took effect
	auto over_research_opt = manager.find_subagent_by_name("research");
	assert(over_research_opt.has_value());
	assert(over_research_opt->description == "Overridden research description");
	assert(over_research_opt->read_only == false);
	assert(over_research_opt->system_prompt == "Overridden system prompt.");

	// Clean up animation
	agent_animation_registry::get_instance().unregister_animation("custom-agent");

	// Clean up
	std::filesystem::remove_all(temp_home);
}

void test_subagent_manager_dynamic()
{
	auto& manager = subagent_manager::get_instance();
	manager.initialize();

	std::string plugin_agent_md = 
		"---\n"
		"name: plugin-agent\n"
		"description: Dynamic plugin subagent\n"
		"read_only: true\n"
		"---\n"
		"Plugin subagent system prompt.\n";

	// Register it
	manager.register_subagent("plugin-agent", plugin_agent_md);

	// Verify it can be found
	auto sa_opt = manager.find_subagent_by_name("plugin-agent");
	assert(sa_opt.has_value());
	assert(sa_opt->description == "Dynamic plugin subagent");
	assert(sa_opt->read_only == true);
	assert(sa_opt->system_prompt == "Plugin subagent system prompt.");
	assert(sa_opt->file_path == "plugin://plugin-agent");

	// Unregister it
	manager.unregister_subagent("plugin-agent");

	// Verify it is gone
	auto sa_gone = manager.find_subagent_by_name("plugin-agent");
	assert(!sa_gone.has_value());

	// Test dynamic subagent with animation JSON
	std::string anim_json = "{\"DurMovie\": {\"version\": 1, \"frames\": []}}";
	manager.register_subagent("anim-agent", plugin_agent_md, anim_json);

	auto sa_anim = manager.find_subagent_by_name("anim-agent");
	assert(sa_anim.has_value());
	assert(sa_anim->animation_name == "anim-agent");

	// Verify animation is registered in registry
	auto anim_data = agent_animation_registry::get_instance().get_animation("anim-agent");
	assert(anim_data != nullptr);

	// Unregister
	manager.unregister_subagent("anim-agent");

	// Verify animation is gone from registry
	auto anim_gone = agent_animation_registry::get_instance().get_animation("anim-agent");
	assert(anim_gone == nullptr);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_subagent_manager_basic();
	test_subagent_manager_dynamic();
	std::cout << "subagent_manager tests passed.\n";
	return 0;
}
