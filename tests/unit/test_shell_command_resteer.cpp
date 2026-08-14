// test_shell_command_resteer.cpp
//
// Unit tests for shell command regex re-steering engine.

#include "test_watchdog.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "tools/run_shell_command/shell_command_resteer.h"
#include <cassert>
#include <iostream>
#include <string>

int main()
{
	test_watchdog::setup_watchdog(30);

	// 1. Direct evaluate_shell_command_resteer unit tests
	{
		// Test Discovery
		auto rec1 = tools::evaluate_shell_command_resteer("meson test --list");
		assert(rec1.matched);
		assert(rec1.confidence >= 0.90);
		assert(rec1.suggested_tool.find("system://project/testlist.md") != std::string::npos);

		auto rec1_grep = tools::evaluate_shell_command_resteer("cd /home/user/project && meson test --list | grep -i run_shell");
		assert(rec1_grep.matched);
		assert(rec1_grep.suggested_tool.find("system://project/testlist.md?search=run_shell") != std::string::npos);

		// Unit Test Execution
		auto rec2 = tools::evaluate_shell_command_resteer("./build/test_available_tests");
		assert(rec2.matched);
		assert(rec2.suggested_tool.find("fs_run_tests(test_names=[\"test_available_tests\"])") != std::string::npos);

		// File Reading (sed / head)
		auto rec3 = tools::evaluate_shell_command_resteer("sed -n '20,40p' src/main.cpp");
		assert(rec3.matched);
		assert(rec3.suggested_tool.find("fs_read_lines(path=\"src/main.cpp\", start_line=20, end_line=40)") != std::string::npos);

		// Code Search (grep)
		auto rec4 = tools::evaluate_shell_command_resteer("grep -rn \"research_agent\" src/");
		assert(rec4.matched);
		assert(rec4.suggested_tool.find("fs_grep_files(pattern=\"research_agent\", path=\"src/\")") != std::string::npos);

		// Git status / diff / log
		auto rec5 = tools::evaluate_shell_command_resteer("git status");
		assert(rec5.matched);
		assert(rec5.suggested_tool.find("git_status()") != std::string::npos);
	}

	// 2. Integration test via tool_registry & run_shell_command
	{
		agentlib::tool_registry &registry = agentlib::tool_registry::get_instance();
		agentlib::tool_context ctx;
		ctx.fs_security.set_working_directory(std::filesystem::current_path());
		ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

		// A) Standard invocation without force -> should be AUTO-DENIED
		nlohmann::json deny_args = {{"command", "meson test --list"}};
		auto prep_deny = registry.prepare_tool("run_shell_command", deny_args.dump(), ctx);
		assert(prep_deny.tool == nullptr && "run_shell_command without force for 'meson test --list' must be auto-denied.");
		assert(prep_deny.error_message.find("Denied: Shell command matches native tool recommendation") != std::string::npos);
		assert(prep_deny.error_message.find("system://project/testlist.md") != std::string::npos);
		assert(prep_deny.error_message.find("force: true") != std::string::npos);

		// B) Override with force: true -> should be ACCEPTED for user approval
		nlohmann::json force_args = {{"command", "meson test --list"}, {"force", true}};
		auto prep_force = registry.prepare_tool("run_shell_command", force_args.dump(), ctx);
		assert(prep_force.tool != nullptr && "run_shell_command with force: true must bypass auto-denial.");
		assert(prep_force.error_message.empty());
	}

	std::cout << "All test_shell_command_resteer tests passed successfully!\n";
	return 0;
}
