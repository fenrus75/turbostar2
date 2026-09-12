// Tested source file: src/tools/git_add/git_add_security.cpp
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

static void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	temp_git_repo repo("add");
	std::string test_dir = repo.get_path();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, access_type::read);
	ctx.fs_security.add_allowed_root(test_dir, access_type::write);

	std::cout << "Testing git_add..." << std::endl;

	// 1. Canonical paths array: stage files
	{
		std::filesystem::path f1 = std::filesystem::path(test_dir) / "f1.txt";
		std::filesystem::path f2 = std::filesystem::path(test_dir) / "f2.txt";
		write_file(f1, "f1\n");
		write_file(f2, "f2\n");

		nlohmann::json args = {{"paths", {"f1.txt", "f2.txt"}}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 2 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  f1.txt") != std::string::npos);
		assert(status.find("A  f2.txt") != std::string::npos);

		// Unstage to reset state
		fs_utils::execute_command_sync("git reset HEAD f1.txt f2.txt");
		std::filesystem::remove(f1);
		std::filesystem::remove(f2);
	}

	// 2. Single string in paths: "f_str.txt"
	{
		std::filesystem::path f = std::filesystem::path(test_dir) / "f_str.txt";
		write_file(f, "content\n");

		nlohmann::json args = {{"paths", "f_str.txt"}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 1 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  f_str.txt") != std::string::npos);

		fs_utils::execute_command_sync("git reset HEAD f_str.txt");
		std::filesystem::remove(f);
	}

	// 3. Alias 'files' with array: ["file_a.txt", "file_b.txt"]
	{
		std::filesystem::path fa = std::filesystem::path(test_dir) / "file_a.txt";
		std::filesystem::path fb = std::filesystem::path(test_dir) / "file_b.txt";
		write_file(fa, "fa\n");
		write_file(fb, "fb\n");

		nlohmann::json args = {{"files", {"file_a.txt", "file_b.txt"}}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 2 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  file_a.txt") != std::string::npos);
		assert(status.find("A  file_b.txt") != std::string::npos);

		fs_utils::execute_command_sync("git reset HEAD file_a.txt file_b.txt");
		std::filesystem::remove(fa);
		std::filesystem::remove(fb);
	}

	// 4. Alias 'files' with single string
	{
		std::filesystem::path f = std::filesystem::path(test_dir) / "file_single.txt";
		write_file(f, "single\n");

		nlohmann::json args = {{"files", "file_single.txt"}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 1 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  file_single.txt") != std::string::npos);

		fs_utils::execute_command_sync("git reset HEAD file_single.txt");
		std::filesystem::remove(f);
	}

	// 5. Singular alias 'path'
	{
		std::filesystem::path f = std::filesystem::path(test_dir) / "path_alias.txt";
		write_file(f, "path alias\n");

		nlohmann::json args = {{"path", "path_alias.txt"}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 1 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  path_alias.txt") != std::string::npos);

		fs_utils::execute_command_sync("git reset HEAD path_alias.txt");
		std::filesystem::remove(f);
	}

	// 6. Singular alias 'file'
	{
		std::filesystem::path f = std::filesystem::path(test_dir) / "file_alias.txt";
		write_file(f, "file alias\n");

		nlohmann::json args = {{"file", "file_alias.txt"}};
		std::string result = registry.execute_tool("git_add", args.dump(), ctx);
		assert(result.find("Successfully staged 1 path(s)") != std::string::npos);

		std::string status = fs_utils::execute_command_sync("git status --porcelain");
		assert(status.find("A  file_alias.txt") != std::string::npos);

		fs_utils::execute_command_sync("git reset HEAD file_alias.txt");
		std::filesystem::remove(f);
	}

	// 7. Validation failure: empty paths array
	{
		nlohmann::json args = {{"paths", nlohmann::json::array()}};
		auto prep = registry.prepare_tool("git_add", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 8. Security failure: path outside allowed root
	{
		nlohmann::json args = {{"paths", {"../../../etc/passwd"}}};
		auto prep = registry.prepare_tool("git_add", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 9. Validation failure: missing arguments
	{
		nlohmann::json args = nlohmann::json::object();
		auto prep = registry.prepare_tool("git_add", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_add tests passed successfully.\n";
	return 0;
}
