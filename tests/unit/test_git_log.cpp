// Tested source file: src/tools/git_log/git_log_entry.cpp, src/tools/git_log/git_log_security.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	temp_git_repo repo("log");

	// Add a second commit modifying only file_b.txt
	fs_utils::execute_command_sync("touch file_b.txt");
	fs_utils::execute_command_sync("git add file_b.txt");
	fs_utils::execute_command_sync("git commit -m \"commit for file_b\"");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_log..." << std::endl;

	// 1. Success case: retrieve git log for entire repo
	{
		std::string result = registry.execute_tool("git_log", "{}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("Process exited with code 0") != std::string::npos);
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("initial commit") != std::string::npos);
	}

	// 1b. Success case: retrieve git log with explicit limit
	{
		std::string result = registry.execute_tool("git_log", "{\"limit\": 2}", ctx);
		std::cout << "Result (limit=2):\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("Process exited with code 0") != std::string::npos);
	}

	// 1c. Success case: filter git log by path (only file_b.txt)
	{
		std::string result = registry.execute_tool("git_log", "{\"path\": \"file_b.txt\"}", ctx);
		std::cout << "Result (path=file_b.txt):\n" << result << std::endl;
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("initial commit") == std::string::npos);
	}

	// 1d. Success case: filter git log using global alias 'file_path' (only dummy_initial.txt)
	{
		std::string result = registry.execute_tool("git_log", "{\"file_path\": \"dummy_initial.txt\"}", ctx);
		std::cout << "Result (file_path=dummy_initial.txt):\n" << result << std::endl;
		assert(result.find("initial commit") != std::string::npos);
		assert(result.find("commit for file_b") == std::string::npos);
	}

	// 1e. Success case: path="." shows all commits
	{
		std::string result = registry.execute_tool("git_log", "{\"path\": \".\"}", ctx);
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("initial commit") != std::string::npos);
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
