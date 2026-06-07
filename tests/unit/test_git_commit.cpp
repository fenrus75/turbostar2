#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"
#include "git_test_helper.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	if (!path.parent_path().empty()) {
		std::filesystem::create_directories(path.parent_path());
	}
	std::ofstream out(path);
	out << content;
}

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("commit");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_commit..." << std::endl;

	// 1. Failure case: nothing to commit
	{
		nlohmann::json args = {{"message", "should fail because nothing is staged"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result nothing staged: " << result << std::endl;
		assert(result.find("No staged changes found") != std::string::npos);
	}

	// 2. Success case: stage a file, commit it
	{
		std::string test_dir = repo.get_path();
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "dummy_commit_test.txt";
		write_file(dummy_file, "dummy content modified");
		fs_utils::execute_command_sync("git -C {} add dummy_commit_test.txt", test_dir);

		nlohmann::json args = {{"message", "test: dummy commit for unit test"}};
		std::string result = registry.execute_tool("git_commit", args.dump(), ctx);
		std::cout << "Result success commit: " << result << std::endl;
		assert(result.find("Successfully created commit") != std::string::npos);
	}

	// 3. Validation failure: empty commit message
	{
		nlohmann::json args = {{"message", ""}};
		auto prep = registry.prepare_tool("git_commit", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"message", "test msg"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_commit", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_commit tests passed successfully.\n";
	return 0;
}
