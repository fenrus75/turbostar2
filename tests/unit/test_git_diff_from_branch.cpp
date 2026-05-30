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

	std::cout << "Testing git_diff_from_branch..." << std::endl;

	// 1. Success case: retrieve diff against HEAD
	{
		nlohmann::json args = {{"branch_name", "HEAD"}};
		std::string result = registry.execute_tool("git_diff_from_branch", args.dump(), ctx);
		std::cout << "Result against HEAD:\n" << result << std::endl;
		assert(!result.empty());
		// Since we have local unstaged changes, diff against HEAD should show them
		assert(result.find("diff --git") != std::string::npos);
	}

	// 2. Validation failure: unsafe shell characters
	{
		nlohmann::json args = {{"branch_name", "HEAD; rm -rf /"}};
		auto prep = registry.prepare_tool("git_diff_from_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"branch_name", "HEAD"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_diff_from_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_diff_from_branch tests passed successfully.\n";
	return 0;
}
