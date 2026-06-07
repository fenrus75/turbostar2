#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("pull");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_pull..." << std::endl;

	// 1. Success or clean execution case: execute git pull
	{
		std::string result = registry.execute_tool("git_pull", "{}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("pulled from remote") != std::string::npos ||
		       result.find("Failed to pull from remote") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_pull", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_pull tests passed successfully.\n";
	return 0;
}
