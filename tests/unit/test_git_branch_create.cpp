#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("branch_create");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_branch_create..." << std::endl;

	// 1. Success case: create a valid branch
	{
		std::string branch_name = "test-new-branch-xyz";
		// Clean up branch in case it was left over from a previous failed run
		fs_utils::execute_command_sync("git branch -D {} 2>/dev/null", branch_name);

		nlohmann::json args = {{"branch_name", branch_name}};
		std::string result = registry.execute_tool("git_branch_create", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Successfully created branch") != std::string::npos);

		// Verify it was created and delete it
		std::string check = fs_utils::execute_command_sync("git branch --list {}", branch_name);
		assert(check.find(branch_name) != std::string::npos);
		fs_utils::execute_command_sync("git branch -D {}", branch_name);
	}

	// 2. Validation failure: unsafe shell characters
	{
		nlohmann::json args = {{"branch_name", "branch; rm -rf /"}};
		auto prep = registry.prepare_tool("git_branch_create", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"branch_name", "test-branch"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_branch_create", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_branch_create tests passed successfully.\n";
	return 0;
}
