#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/fs_utils.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing git_diff_staged..." << std::endl;

	// 1. Success case: execute with no staged changes
	{
		nlohmann::json args = {{"path", "."}};
		std::string result = registry.execute_tool("git_diff_staged", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(!result.empty());
	}

	// 2. Success case: stage a file and retrieve staged diff
	{
		std::filesystem::path dummy_file = std::filesystem::path(project_root) / "dummy_diff_staged.txt";
		write_file(dummy_file, "staged change content\n");
		fs_utils::execute_command_sync("git add dummy_diff_staged.txt");

		nlohmann::json args = {{"path", "dummy_diff_staged.txt"}};
		std::string result = registry.execute_tool("git_diff_staged", args.dump(), ctx);
		std::cout << "Staged diff result:\n" << result << std::endl;
		assert(result.find("staged change content") != std::string::npos);

		// Clean up
		fs_utils::execute_command_sync("git restore --staged dummy_diff_staged.txt");
		std::filesystem::remove(dummy_file);
	}

	// 3. Security failure: path outside allowed root
	{
		nlohmann::json args = {{"path", "../../../etc/passwd"}};
		auto prep = registry.prepare_tool("git_diff_staged", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"path", "."}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_diff_staged", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 5. Verify description contains "git diff"
	{
		nlohmann::json tools = registry.get_tools_json();
		bool found = false;
		for (const auto &tool : tools) {
			if (tool["function"]["name"] == "git_diff_staged") {
				std::string desc = tool["function"]["description"];
				assert(desc.find("git diff") != std::string::npos);
				found = true;
				break;
			}
		}
		assert(found);
	}

	std::cout << "git_diff_staged tests passed successfully.\n";
	return 0;
}
