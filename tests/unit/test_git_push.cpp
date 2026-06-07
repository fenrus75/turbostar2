#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/event_queue.h"
#include "../../src/project_manager.h"

#include "git_test_helper.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	temp_git_repo repo("push");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	ctx.fs_security.set_working_directory(repo.get_path());
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::read);
	ctx.fs_security.add_allowed_root(repo.get_path(), access_type::write);
	event_queue q;
	ctx.queue = &q;

	std::cout << "Testing git_push..." << std::endl;

	// 1. Clean execution case (force = false): execute git push
	{
		nlohmann::json args = {{"force", false}};
		std::string result = registry.execute_tool("git_push", args.dump(), ctx);
		std::cout << "Result force=false:\n" << result << std::endl;
		assert(!result.empty());
		assert(result.find("pushed to remote") != std::string::npos || result.find("Failed to push") != std::string::npos);
	}

	// 2. Interactive case (force = true) - Allow
	{
		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						assert(ev->payload.find("FORCE push") != std::string::npos);
						ev->prompt_promise->set_value("Allow");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"force", true}};
		std::string result = registry.execute_tool("git_push", args.dump(), ctx);
		worker.join();
		std::cout << "Result force=true Allow:\n" << result << std::endl;
		assert(result.find("pushed to remote") != std::string::npos || result.find("Failed to push") != std::string::npos);
	}

	// 3. Interactive case (force = true) - Deny
	{
		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::prompt_user) {
						ev->prompt_promise->set_value("Deny");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		nlohmann::json args = {{"force", true}};
		std::string result = registry.execute_tool("git_push", args.dump(), ctx);
		worker.join();
		std::cout << "Result force=true Deny:\n" << result << std::endl;
		assert(result.find("Permission denied by user") != std::string::npos);
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"force", false}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("git_push", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	std::cout << "git_push tests passed successfully.\n";
	return 0;
}
