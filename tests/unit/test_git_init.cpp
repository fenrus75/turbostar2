#include <cassert>
#include <filesystem>
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

	temp_git_repo repo("init");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_init..." << std::endl;

	std::filesystem::path git_path = std::filesystem::path(repo.get_path()) / ".git";

	// Remove the pre-initialized .git so we can test git_init from scratch
	std::filesystem::remove_all(git_path);

	// A. Success path
	std::string result = registry.execute_tool("git_init", "{}", ctx);
	std::cout << "Success path result: " << result << std::endl;
	assert(result.find("Successfully initialized") != std::string::npos);
	assert(std::filesystem::exists(git_path));

	// B. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_init", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 2. Failure case: .git already exists in project root
	{
		auto prep = registry.prepare_tool("git_init", "{}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("already exists") != std::string::npos);
	}

	std::cout << "git_init tests passed successfully.\n";
	return 0;
}
