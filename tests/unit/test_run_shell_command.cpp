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

using namespace agentlib;

int main()
{
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

	// 1. Success case: execute command and permit "Once"
	{
		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						assert(ev->payload.find("echo 'hello shell'") != std::string::npos);
						ev->prompt_promise->set_value("Once");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"command", "echo 'hello shell'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
		worker.join();
		std::cout << "Result: " << result << std::endl;
		assert(result.find("hello shell") != std::string::npos);
	}

	// 2. Denied case: permission denied by user ("Deny")
	{
		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						assert(ev->payload.find("echo 'deny me'") != std::string::npos);
						ev->prompt_promise->set_value("Deny");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"command", "echo 'deny me'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
		worker.join();
		std::cout << "Result: " << result << std::endl;
		assert(result.find("Permission denied by user") != std::string::npos);
	}

	// 3. Validation failure: empty command
	{
		nlohmann::json args = {{"command", ""}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("empty") != std::string::npos);
	}

	// 4. Validation failure: ANSI escape sequence in command
	{
		nlohmann::json args = {{"command", "echo \x1b[31mred\x1b[0m"}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("ANSI escape sequences") != std::string::npos);
	}

	// 5. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"command", "echo 'hello'"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("run_shell_command", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 6. Queue missing failure
	{
		tool_context no_q_ctx = ctx;
		no_q_ctx.queue = nullptr;
		nlohmann::json args = {{"command", "echo 'no queue'"}};
		std::string result = registry.execute_tool("run_shell_command", args.dump(), no_q_ctx);
		std::cout << "Result with no queue: " << result << std::endl;
		assert(result.find("No event queue available") != std::string::npos);
	}

	// 7. Test shell_display_access setting propagation
	{
		std::cout << "Testing shell_display_access setting..." << std::endl;
		setenv("DISPLAY", ":99", 1);
		setenv("XAUTHORITY", "/tmp/mock_xauth", 1);
		std::ofstream f("/tmp/mock_xauth");
		f.close();

		// Case 7a: shell_display_access is false -> DISPLAY should NOT be passed
		config_manager::get_instance().set_shell_display_access(false);
		{
			std::thread worker([&q]() {
				while (true) {
					auto ev = q.pop();
					if (ev) {
						if (ev->type == event_type::prompt_user) {
							ev->prompt_promise->set_value("Once");
							break;
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			});

			nlohmann::json args = {{"command", "echo \"val:${DISPLAY}\""}};
			std::string result = registry.execute_tool("run_shell_command", args.dump(), ctx);
			worker.join();
			std::cout << "Result with display disabled: " << result << std::endl;
			assert(result.find(":99") == std::string::npos);
		}

		// Case 7b: shell_display_access is true -> DISPLAY should be passed
		config_manager::get_instance().set_shell_display_access(true);
		{
			std::thread worker([&q]() {
				while (true) {
					auto ev = q.pop();
					if (ev) {
						if (ev->type == event_type::prompt_user) {
							ev->prompt_promise->set_value("Once");
							break;
						}
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			});

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

	std::cout << "run_shell_command tests passed successfully.\n";
	return 0;
}
