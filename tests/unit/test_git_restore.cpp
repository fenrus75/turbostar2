#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

void write_file(const std::filesystem::path &path, const std::string &content)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream out(path);
	out << content;
}

std::string read_file(const std::filesystem::path &path)
{
	std::ifstream in(path);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("restore");
	std::string test_dir = repo.get_path();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, access_type::read);
	ctx.fs_security.add_allowed_root(test_dir, access_type::write);

	std::cout << "Testing git_restore..." << std::endl;

	// 1. Success case: create a tracked file, modify it, restore it
	{
		std::filesystem::path dummy_file = std::filesystem::path(test_dir) / "temp_restore_test.txt";
		write_file(dummy_file, "hello\n");
		fs_utils::execute_command_sync("git add temp_restore_test.txt");
		fs_utils::execute_command_sync("git commit -m 'test: temporary commit for restore'");

		// Modify file
		write_file(dummy_file, "modified content\n");

		// Restore file
		nlohmann::json args = {{"path", "temp_restore_test.txt"}};
		std::string result = registry.execute_tool("git_restore", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Successfully restored path") != std::string::npos);

		// Read and check content is restored
		std::string content = read_file(dummy_file);
		assert(content == "hello\n");

		// Clean up
		fs_utils::execute_command_sync("git reset HEAD~1");
		std::filesystem::remove(dummy_file);
	}

	// 2. Security failure: path outside allowed root
	{
		nlohmann::json args = {{"path", "../../../etc/passwd"}};
		auto prep = registry.prepare_tool("git_restore", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"path", "meson.build"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_restore", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_restore tests passed successfully.\n";
	return 0;
}
