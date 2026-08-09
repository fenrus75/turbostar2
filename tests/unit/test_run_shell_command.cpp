#include "test_watchdog.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "../../src/config_manager.h"

#include "../../src/tools/terminal_command_runner.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);
	ctx.queue = &q;

	std::cout << "Testing run_shell_command..." << std::endl;

	// Bound every approval-handling worker with a deadline so that if the tool under test
	// ever fails to enqueue a prompt (e.g. an early validation/queue error), the worker
	// fails fast with a clear diagnostic instead of spinning until the watchdog fires.
	auto start_prompt_worker = [&q](const std::string &expected_fragment, std::string response) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		std::thread worker([&q, expected_fragment, response, deadline]() {
			while (std::chrono::steady_clock::now() < deadline) {
				auto ev = q.pop();
				if (ev && ev->type == event_type::prompt_user) {
					assert(ev->payload.find(expected_fragment) != std::string::npos);
					ev->prompt_promise->set_value(response);
					return;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			assert(false && "worker timed out waiting for a prompt_user event");
		});
		return worker;
	};

	// 1. Success case: execute command and permit "Once"
	{
		std::thread worker = start_prompt_worker("echo 'hello shell'", "Once");

		nlohmann::json args = {{"command", "echo 'hello shell'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
		worker.join();
		std::cout << "Result: " << result << std::endl;
		assert(result.find("hello shell") != std::string::npos);
	}

	// 2. Denied case: permission denied by user ("Deny")
	{
		std::thread worker = start_prompt_worker("echo 'deny me'", "Deny");

		nlohmann::json args = {{"command", "echo 'deny me'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
		worker.join();
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Permission denied by user") != std::string::npos);
	}

	// 3. Approval latency is reported back to the LLM on an interactive ("Once") approval,
	//    and (with command logging enabled) the approved command is appended to the log file.
	{
		std::thread worker = start_prompt_worker("approval timing", "Once");

		// HOME is already isolated by setup_watchdog, so the command log is written under
		// that isolated home. Use the RAII guard's path to locate it (no manual setenv).
		const std::string isolated_home = test_watchdog::get_global_test_home()->get_path();

		config_manager::get_instance().set_log_shell_commands(true);
		nlohmann::json args = {{"command", "echo 'approval timing'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
		worker.join();
		std::cout << "Result (approval timing): " << result << std::endl;
		assert(result.find("approval timing") != std::string::npos);
		assert(result.find("(approved by user in") != std::string::npos);

		// Command logging writes a line into the dedicated log file under the isolated HOME
		std::string log_file = isolated_home + "/.cache/turbostar/shell_commands.log";
		std::ifstream log_in(log_file);
		assert(log_in.is_open());
		std::string log_line;
		bool found = false;
		while (std::getline(log_in, log_line)) {
			if (log_line.find("approval timing") != std::string::npos) {
				found = true;
			}
		}
		assert(found && "approved command should be appended to the shell commands log");
		config_manager::get_instance().set_log_shell_commands(false);
	}

	// 4. Validation failure: empty command
	{
		nlohmann::json args = {{"command", ""}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("empty") != std::string::npos);
	}

	// 5. Validation failure: ANSI escape sequence in command
	{
		nlohmann::json args = {{"command", "echo \x1b[31mred\x1b[0m"}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("ANSI escape sequences") != std::string::npos);
	}

	// 6. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"command", "echo 'hello'"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 7. Queue missing failure
	{
		tool_context no_q_ctx = ctx;
		no_q_ctx.queue = nullptr;
		nlohmann::json args = {{"command", "echo 'no queue'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), no_q_ctx);
		std::cout << "Result with no queue: " << result << std::endl;
		assert(result.find("No event queue available") != std::string::npos);
	}

	// 8. Test shell_display_access setting propagation
	{
		std::cout << "Testing shell_display_access setting..." << std::endl;
		setenv("DISPLAY", ":99", 1);
		setenv("XAUTHORITY", "/tmp/mock_xauth", 1);
		std::ofstream f("/tmp/mock_xauth");
		f.close();

		// Case 8a: shell_display_access is false -> DISPLAY should NOT be passed
		config_manager::get_instance().set_shell_display_access(false);
		{
			std::thread worker = start_prompt_worker("val", "Once");

			nlohmann::json args = {{"command", "echo \"val:${DISPLAY}\""}};
			std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
			worker.join();
			std::cout << "Result with display disabled: " << result << std::endl;
			assert(result.find(":99") == std::string::npos);
		}

		// Case 8b: shell_display_access is true -> DISPLAY should be passed
		config_manager::get_instance().set_shell_display_access(true);
		{
			std::thread worker = start_prompt_worker("val", "Once");

			nlohmann::json args = {{"command", "echo \"val:${DISPLAY}\""}};
			std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
			worker.join();
			std::cout << "Result with display enabled: " << result << std::endl;
			assert(result.find(":99") != std::string::npos);
		}

		std::filesystem::remove("/tmp/mock_xauth");
		unsetenv("DISPLAY");
		unsetenv("XAUTHORITY");
	}

	// 9. Test terminal_command_runner trigger_update callback periodic firing
	{
		std::cout << "Testing terminal_command_runner trigger_update callback periodic firing..." << std::endl;
		int trigger_count = 0;
		auto interaction = std::make_shared<agentlib::interaction_terminal>("test", "test");
		tools::terminal_command_runner runner(interaction, [&]() {
			trigger_count++;
		});
		runner.apply_build_profile();
		runner.execute("sleep 0.3");
		std::cout << "Trigger count: " << trigger_count << std::endl;
		assert(trigger_count > 0 && "trigger_update callback should have been called at least once during execution!");
	}

	std::cout << "run_shell_command tests passed successfully.\n";
	return 0;
}
