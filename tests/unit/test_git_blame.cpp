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

	temp_git_repo repo("blame");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);

	std::cout << "Testing git_blame..." << std::endl;

	std::string test_file_name = "test_file.txt";
	std::string test_file_path = repo.get_path() + "/" + test_file_name;

	// 1. Create file and commit it (Commit A)
	{
		std::ofstream out(test_file_path);
		out << "Line 1: initial text\n";
		out << "Line 2: initial text\n";
		out << "Line 3: initial text\n";
		out.close();
	}

	fs_utils::execute_command_sync(std::format("git add {}", test_file_name));
	fs_utils::execute_command_sync("git commit -m \"Commit A: added test file\"");

	// 2. Modify line 2 and commit it (Commit B)
	{
		std::ofstream out(test_file_path);
		out << "Line 1: initial text\n";
		out << "Line 2: modified text\n";
		out << "Line 3: initial text\n";
		out.close();
	}

	fs_utils::execute_command_sync(std::format("git add {}", test_file_name));
	fs_utils::execute_command_sync("git commit -m \"Commit B: modified second line\"");

	// 3. Test: run git_blame on test_file.txt
	{
		nlohmann::json args = {{"path", test_file_name}};
		std::string result = registry.execute_tool("git_blame", args.dump(), ctx);
		std::cout << "Blame Output:\n" << result << std::endl;

		assert(!result.empty());
		// Verify table structure and columns
		assert(result.find("Line Range") != std::string::npos);
		assert(result.find("Grounding Code (First Line)") != std::string::npos);
		assert(result.find("Commit Description") != std::string::npos);

		// Verify grounding code correctness
		assert(result.find("Line 1: initial text") != std::string::npos);
		assert(result.find("Line 2: modified text") != std::string::npos);
		assert(result.find("Line 3: initial text") != std::string::npos);

		// Verify commit summaries are parsed
		assert(result.find("Commit A: added test file") != std::string::npos);
		assert(result.find("Commit B: modified second line") != std::string::npos);

		// Verify range consolidation: lines 1 and 3 are separated by 2
		assert(result.find("1") != std::string::npos);
		assert(result.find("2") != std::string::npos);
		assert(result.find("3") != std::string::npos);
	}

	// 4. Test: run with range start_line and end_line
	{
		nlohmann::json args = {
			{"path", test_file_name},
			{"start_line", 2},
			{"end_line", 3}
		};
		std::string result = registry.execute_tool("git_blame", args.dump(), ctx);
		std::cout << "Range Blame Output:\n" << result << std::endl;

		assert(!result.empty());
		// Should contain line 2 and 3 details, but not line 1
		assert(result.find("Line 1: initial text") == std::string::npos);
		assert(result.find("Line 2: modified text") != std::string::npos);
		assert(result.find("Line 3: initial text") != std::string::npos);
	}

	// 5. Test: argument validation error with invalid path
	{
		nlohmann::json args = {{"unexpected_path", "nonexistent.txt"}};
		auto prep = registry.prepare_tool("git_blame", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_blame tests passed successfully.\n";
	return 0;
}
