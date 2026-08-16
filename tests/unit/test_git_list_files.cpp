#include "test_watchdog.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	temp_git_repo repo("list_files");

	// Create files in the repo
	std::ofstream f1(repo.get_path() + "/file1.txt");
	f1 << "hello 1\n";
	f1.close();

	std::ofstream f2(repo.get_path() + "/file2.cpp");
	f2 << "int main() {}\n";
	f2.close();

	fs_utils::execute_command_sync("git add file1.txt file2.cpp");
	fs_utils::execute_command_sync("git commit -m \"add files\"");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_list_files..." << std::endl;

	// 1. Success case: list all tracked files
	{
		std::string result = registry.execute_tool("git_list_files", "{}", ctx);
		std::cout << "Result:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("file1.txt") != std::string::npos);
		assert(result.find("file2.cpp") != std::string::npos);
	}

	// 2. Filter by pattern
	{
		nlohmann::json args = {{"pattern", ".cpp"}};
		std::string result = registry.execute_tool("git_list_files", args.dump(), ctx);
		assert(result.find("file2.cpp") != std::string::npos);
		assert(result.find("file1.txt") == std::string::npos);
	}

	std::cout << "git_list_files tests passed successfully.\n";
	return 0;
}
