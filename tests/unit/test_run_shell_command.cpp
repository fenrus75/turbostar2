#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <future>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

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

	std::cout << "run_shell_command tests passed successfully.\n";
	return 0;
}
