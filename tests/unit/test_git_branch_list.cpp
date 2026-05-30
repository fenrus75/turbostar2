#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::cout << "Testing git_branch_list..." << std::endl;

	// 1. Success case: execute list branch
	{
		std::string result = registry.execute_tool("git_branch_list", "{}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(!result.empty());
		assert(result.find("## Git Branches") != std::string::npos);

		// Get current branch name and assert it is marked active with checkmark
		std::string current = fs_utils::execute_command_sync("git branch --show-current");
		size_t pos = current.find('\n');
		if (pos != std::string::npos) {
			current = current.substr(0, pos);
		}
		std::cout << "Current branch: " << current << std::endl;
		assert(result.find("✅ | `" + current + "`") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_branch_list", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_branch_list tests passed successfully.\n";
	return 0;
}
