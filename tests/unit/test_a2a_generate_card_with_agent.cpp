#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/command_registry.h"
#include "../../src/agentlib/skill_manager.h"
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/pluginloader.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	(void)command_registry::get_instance();
	(void)skill_manager::get_instance();
	tool_registry &registry = tool_registry::get_instance();

	// Load dynamic plugins
	plugin_loader::get_instance().load_all_plugins();

	// Verify subagent was registered
	auto subagent_opt = subagent_manager::get_instance().find_subagent_by_name("a2acardgenerator");
	assert(subagent_opt.has_value());
	assert(subagent_opt->name == "a2acardgenerator");

	tool_context ctx;
	ctx.properties.active_families = {"a2a", "base"};
	ctx.is_family_active = [](const std::string &family) { return family == "a2a" || family == "base"; };

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing a2a_generate_card_with_agent..." << std::endl;

	// Dispatch card generation tool
	nlohmann::json args = {
	    {"path", "src/plugins/securityagent/securityagent.md"},
	    {"output_path", "tmp://securityagent.card.json"}};

	std::string res = registry.execute_tool("a2a_generate_card_with_agent", args.dump(), ctx);
	std::cout << "Dispatch Result:\n" << res << std::endl;
	assert(res.find("A2A Card Generation Dispatched") != std::string::npos);
	assert(res.find("a2acardgenerator") != std::string::npos);

	std::cout << "a2a_generate_card_with_agent unit test passed successfully!\n";
	return 0;
}
