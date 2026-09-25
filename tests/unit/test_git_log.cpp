// Tested source file: src/tools/git_log/git_log_entry.cpp, src/tools/git_log/git_log_security.cpp
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "test_watchdog.h"

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

	// 1b2. Success case: retrieve git log with alias 'max_count'
	{
		std::string result = registry.execute_tool("git_log", "{\"max_count\": 2}", ctx);
		assert(!result.empty());
		assert(result.find("Process exited with code 0") != std::string::npos);
	}

	// 1b3. Success case: retrieve git log with alias 'n'
	{
		std::string result = registry.execute_tool("git_log", "{\"n\": 2}", ctx);
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

	// 1f. Success case: retrieve specific commit with commit_id
	{
		std::string result = registry.execute_tool("git_log", "{\"commit_id\": \"HEAD\"}", ctx);
		std::cout << "Result (commit_id=HEAD):\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("diff --git") != std::string::npos);
	}

	// 1g. Success case: retrieve commit with alias 'commit' and show_patch: true
	{
		std::string result = registry.execute_tool("git_log", "{\"commit\": \"HEAD\", \"show_patch\": true}", ctx);
		std::cout << "Result (commit=HEAD, show_patch=true):\n" << result << std::endl;
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("diff --git") != std::string::npos);
	}

	// 1h. Success case: git log with show_patch: true
	{
		std::string result = registry.execute_tool("git_log", "{\"limit\": 1, \"show_patch\": true}", ctx);
		std::cout << "Result (limit=1, show_patch=true):\n" << result << std::endl;
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("diff --git") != std::string::npos);
	}

	// 1i. Success case: git log with stat: true
	{
		std::string result = registry.execute_tool("git_log", "{\"limit\": 1, \"stat\": true}", ctx);
		std::cout << "Result (limit=1, stat=true):\n" << result << std::endl;
		assert(result.find("commit for file_b") != std::string::npos);
		assert(result.find("file_b.txt") != std::string::npos);
	}

	// 2. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_log", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 2b. Validation failure: invalid characters in commit_id
	{
		nlohmann::json args = {{"commit_id", "HEAD; rm -rf /"}};
		auto prep = registry.prepare_tool("git_log", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_log tests passed successfully.\n";
	return 0;
}
