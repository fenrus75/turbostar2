#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
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

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();
	ctx.queue = &q;

	std::cout << "Testing enter_plan_mode and exit_plan_mode..." << std::endl;
	{
		// 1. Enter plan mode
		std::string enter_res = registry.execute_tool("enter_plan_mode", "{\"plan_file\": \"test_plan_doc.md\"}", ctx);
		assert(agent->is_planning() == true);
		assert(agent->get_plan_file() == "test_plan_doc.md");

		// 2. Exit plan mode with user approval
		std::thread worker([&q]() {
			while (true) {
				auto ev = q.pop();
				if (ev) {
					if (ev->type == event_type::approve_plan) {
						ev->prompt_promise->set_value("Approved");
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		});

		std::string exit_res = registry.execute_tool("exit_plan_mode",
			"{\"plan_title\": \"My Title\", \"plan_summary\": \"My Summary\"}", ctx);
		worker.join();

		std::cout << "Exit response received: " << exit_res << std::endl;
		assert(exit_res.find("test_plan_doc.md") != std::string::npos);
		assert(agent->is_planning() == false);
	}

	return 0;
}
