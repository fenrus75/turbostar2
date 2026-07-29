#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

#include "../../src/agentlib/command_registry.h"
#include "../../src/agentlib/skill_manager.h"
#include "../../src/pluginloader.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	(void)command_registry::get_instance();
	(void)skill_manager::get_instance();
	tool_registry &registry = tool_registry::get_instance();

	// Load all dynamic plugins including a2a
	plugin_loader::get_instance().load_all_plugins();

	tool_context ctx;
	ctx.properties.active_families = {"a2a", "base"};
	ctx.is_family_active = [](const std::string &family) { return family == "a2a" || family == "base"; };

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing a2a_validate_card..." << std::endl;

	// 1. Success case: Valid Card JSON string
	{
		std::string valid_card = R"({
			"protocol_version": "1.0",
			"name": "securityagent",
			"description": "Security Audit Subagent for code review.",
			"version": "1.0.0",
			"skills": ["security-audit", "vulnerability-scanning"],
			"input_schema": {
				"type": "object",
				"properties": {
					"files": { "type": "array", "items": { "type": "string" } }
				},
				"required": ["files"]
			},
			"output_schema": {
				"type": "object",
				"properties": {
					"report": { "type": "string" }
				}
			}
		})";

		nlohmann::json args = {{"card_data", valid_card}};
		std::string res = registry.execute_tool("a2a_validate_card", args.dump(), ctx);
		std::cout << "Valid Card Result:\n" << res << std::endl;
		assert(res.find("Status**: ✅ VALID") != std::string::npos);
		assert(res.find("securityagent") != std::string::npos);
	}

	// 2. Failure case: Invalid Card JSON string (Missing name and description)
	{
		std::string invalid_card = R"({
			"version": "1.0.0",
			"input_schema": "not an object"
		})";

		nlohmann::json args = {{"card_data", invalid_card}};
		std::string res = registry.execute_tool("a2a_validate_card", args.dump(), ctx);
		std::cout << "Invalid Card Result:\n" << res << std::endl;
		assert(res.find("Status**: ❌ INVALID") != std::string::npos);
		assert(res.find("Missing or empty required string field: `name`") != std::string::npos);
		assert(res.find("Field `input_schema` must be a valid JSON Schema object") != std::string::npos);
	}

	// 3. Failure case: No parameters provided
	{
		nlohmann::json args = nlohmann::json::object();
		auto prep = registry.prepare_tool("a2a_validate_card", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "a2a_validate_card unit tests passed successfully!\n";
	return 0;
}
