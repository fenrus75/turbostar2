#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::cout << "Testing git_log..." << std::endl;

	// 1. Success case: retrieve git log
	{
		std::string result = registry.execute_tool("git_log", "{}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("Fix") != std::string::npos || result.find("Add") != std::string::npos || result.find("git_init") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_log", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_log tests passed successfully.\n";
	return 0;
}
