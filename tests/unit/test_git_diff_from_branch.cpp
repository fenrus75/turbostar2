#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

#include <fstream>

#include "git_test_helper.h"

using namespace agentlib;

std::string read_file(const std::string &path)
{
	std::ifstream in(path);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const std::string &path, const std::string &content)
{
	std::ofstream out(path);
	out << content;
}

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("diff_from_branch");
	std::string test_dir = repo.get_path();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(test_dir);
	ctx.fs_security.add_allowed_root(test_dir, access_type::read);
	ctx.fs_security.add_allowed_root(test_dir, access_type::write);

	std::cout << "Testing git_diff_from_branch..." << std::endl;

	// 1. Success case: retrieve diff against HEAD
	{
		// Modify a tracked file to ensure there is a diff against HEAD
		std::string test_file_path = test_dir + "/dummy_initial.txt";
		std::string original_content = read_file(test_file_path);
		write_file(test_file_path, original_content + "\n// temp modification\n");

		nlohmann::json args = {{"branch_name", "HEAD"}};
		std::string result = registry.execute_tool("git_diff_from_branch", args.dump(), ctx);
		std::cout << "Result against HEAD:\n" << result << std::endl;

		// Restore the file
		write_file(test_file_path, original_content);

		assert(!result.empty());
		assert(result.find("diff --git") != std::string::npos);
	}

	// 2. Validation failure: unsafe shell characters
	{
		nlohmann::json args = {{"branch_name", "HEAD; rm -rf /"}};
		auto prep = registry.prepare_tool("git_diff_from_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 3. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"branch_name", "HEAD"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_diff_from_branch", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_diff_from_branch tests passed successfully.\n";
	return 0;
}
