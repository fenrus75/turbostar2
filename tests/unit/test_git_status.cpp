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

	temp_git_repo repo("status");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_status..." << std::endl;

	// 1. Success case: execute git_status
	{
		std::string result = registry.execute_tool("git_status", "{}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("Git Status") != std::string::npos || result.find("Working tree clean") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_status", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_status tests passed successfully.\n";
	return 0;
}
