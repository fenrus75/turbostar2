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

	std::cout << "Testing git_unstage..." << std::endl;

	// 1. Success case: stage a file, then unstage it
	{
		std::filesystem::path dummy_file = std::filesystem::path(project_root) / "temp_unstage_test.txt";
		write_file(dummy_file, "content\n");

		// Stage file
		fs_utils::execute_command_sync("git add temp_unstage_test.txt");

		// Verify it is staged
		std::string status1 = fs_utils::execute_command_sync("git status --porcelain");
		assert(status1.find("A  temp_unstage_test.txt") != std::string::npos);

		// Unstage file
		nlohmann::json args = {{"paths", {"temp_unstage_test.txt"}}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Successfully unstaged") != std::string::npos);

		// Verify it is unstaged
		std::string status2 = fs_utils::execute_command_sync("git status --porcelain");
		assert(status2.find("?? temp_unstage_test.txt") != std::string::npos);

		// Clean up
		std::filesystem::remove(dummy_file);
	}

	// 2. Validation failure: empty paths array
	{
		nlohmann::json args = {{"paths", nlohmann::json::array()}};
		auto prep = registry.prepare_tool("git_unstage", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Security failure: path outside allowed root
	{
		nlohmann::json args = {{"paths", {"../../../etc/passwd"}}};
		auto prep = registry.prepare_tool("git_unstage", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"paths", {"meson.build"}}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_unstage", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_unstage tests passed successfully.\n";
	return 0;
}
