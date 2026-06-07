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

	temp_git_repo repo("diff_unstaged");
	std::string test_dir = repo.get_path();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, access_type::read);
	ctx.fs_security.add_allowed_root(test_dir, access_type::write);

	std::cout << "Testing git_diff_unstaged..." << std::endl;

	// 1. Success case: modify a file and retrieve diff
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "dummy_diff_unstaged.txt";
		write_file(dummy_file, "initial content\n");
		// Commit the dummy file first to track it
		fs_utils::execute_command_sync("git add dummy_diff_unstaged.txt");
		fs_utils::execute_command_sync("git commit -m 'test: temporary commit for diff_unstaged'");

		// Modify it
		write_file(dummy_file, "initial content\nmodified content\n");

		nlohmann::json args = {{"path", "dummy_diff_unstaged.txt"}};
		std::string result = registry.execute_tool("git_diff_unstaged", args.dump(), ctx);
		std::cout << "Unstaged diff result:\n" << result << std::endl;
		assert(result.find("modified content") != std::string::npos);

		// Clean up: reset to previous commit and remove file
		fs_utils::execute_command_sync("git reset HEAD~1");
		std::filesystem::remove(dummy_file);
	}

	// 2. Security failure: path outside allowed root
	{
		nlohmann::json args = {{"path", "../../../etc/passwd"}};
		auto prep = registry.prepare_tool("git_diff_unstaged", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"path", "."}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_diff_unstaged", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 4. Verify description contains "git diff"
	{
		nlohmann::json tools = registry.get_tools_json();
		bool found = false;
		for (const auto &tool : tools) {
			if (tool["function"]["name"] == "git_diff_unstaged") {
				std::string desc = tool["function"]["description"];
				assert(desc.find("git diff") != std::string::npos);
				found = true;
				break;
			}
		}
		assert(found);
	}

	std::cout << "git_diff_unstaged tests passed successfully.\n";
	return 0;
}
