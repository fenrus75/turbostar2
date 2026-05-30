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

	std::cout << "Testing git_checkout_branch..." << std::endl;

	// 1. Success case: create a temporary branch, switch to it, then switch back and delete it
	{
		std::string orig_branch = fs_utils::execute_command_sync("git branch --show-current");
		size_t pos = orig_branch.find('\n');
		if (pos != std::string::npos) {
			orig_branch = orig_branch.substr(0, pos);
		}

		std::string temp_branch = "temp-checkout-xyz";
		fs_utils::execute_command_sync("git branch -D {} 2>/dev/null", temp_branch);
		fs_utils::execute_command_sync("git branch {}", temp_branch);

		// Switch to temp branch
		{
			nlohmann::json args = {{"branch_name", temp_branch}};
			std::string result = registry.execute_tool("git_checkout_branch", args.dump(), ctx);
			std::cout << "Result switch to temp: " << result << std::endl;
			assert(result.find("Successfully switched to branch") != std::string::npos);
		}

		// Switch back to original branch
		{
			nlohmann::json args = {{"branch_name", orig_branch}};
			std::string result = registry.execute_tool("git_checkout_branch", args.dump(), ctx);
			std::cout << "Result switch back: " << result << std::endl;
			assert(result.find("Successfully switched to branch") != std::string::npos);
		}

		// Clean up branch
		fs_utils::execute_command_sync("git branch -D {}", temp_branch);
	}

	// 2. Validation failure: unsafe shell characters
	{
		nlohmann::json args = {{"branch_name", "branch; rm -rf /"}};
		auto prep = registry.prepare_tool("git_checkout_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"branch_name", "main"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_checkout_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_checkout_branch tests passed successfully.\n";
	return 0;
}
