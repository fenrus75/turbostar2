// Tested source file: src/tools/git_unstage/git_unstage_security.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"
#include "test_watchdog.h"

#include "git_test_helper.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	temp_git_repo repo("unstage");
	std::string test_dir = repo.get_path();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, access_type::read);
	ctx.fs_security.add_allowed_root(test_dir, access_type::write);

	std::cout << "Testing git_unstage..." << std::endl;

	// 1. Success case: stage a file, then unstage it using canonical paths array
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "temp_unstage_test.txt";
		write_file(dummy_file, "content\n");

		fs_utils::execute_command_sync("git add temp_unstage_test.txt");

		std::string status1 = fs_utils::execute_command_sync("git status --porcelain");
		assert(status1.find("A  temp_unstage_test.txt") != std::string::npos);

		nlohmann::json args = {{"paths", {"temp_unstage_test.txt"}}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		assert(result.find("Successfully unstaged") != std::string::npos);

		std::string status2 = fs_utils::execute_command_sync("git status --porcelain");
		assert(status2.find("?? temp_unstage_test.txt") != std::string::npos);

		std::filesystem::remove(dummy_file);
	}

	// 1b. Single string in paths
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "unstage_str.txt";
		write_file(dummy_file, "content\n");
		fs_utils::execute_command_sync("git add unstage_str.txt");

		nlohmann::json args = {{"paths", "unstage_str.txt"}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		assert(result.find("Successfully unstaged") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("?? unstage_str.txt") != std::string::npos);

		std::filesystem::remove(dummy_file);
	}

	// 1c. Alias 'files' array
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "unstage_files.txt";
		write_file(dummy_file, "content\n");
		fs_utils::execute_command_sync("git add unstage_files.txt");

		nlohmann::json args = {{"files", {"unstage_files.txt"}}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		assert(result.find("Successfully unstaged") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("?? unstage_files.txt") != std::string::npos);

		std::filesystem::remove(dummy_file);
	}

	// 1d. Alias 'path' single string
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "unstage_path.txt";
		write_file(dummy_file, "content\n");
		fs_utils::execute_command_sync("git add unstage_path.txt");

		nlohmann::json args = {{"path", "unstage_path.txt"}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		assert(result.find("Successfully unstaged") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("?? unstage_path.txt") != std::string::npos);

		std::filesystem::remove(dummy_file);
	}

	// 1e. Alias 'file' single string
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "unstage_file.txt";
		write_file(dummy_file, "content\n");
		fs_utils::execute_command_sync("git add unstage_file.txt");

		nlohmann::json args = {{"file", "unstage_file.txt"}};
		std::string result = registry.execute_tool("git_unstage", args.dump(), ctx);
		assert(result.find("Successfully unstaged") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("?? unstage_file.txt") != std::string::npos);

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
