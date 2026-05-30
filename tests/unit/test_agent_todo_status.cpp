#include <cassert>
#include <iostream>
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

	std::cout << "Testing agent_todo_status..." << std::endl;
	{
		// Spawn a subagent (ID should be 101)
		auto sub = agent->spawn_subagent("ResearchSubagent");
		int sub_id = sub->get_id();
		assert(sub_id == 101);

		// 1. Success case: empty todo list for the subagent
		{
			std::string res = registry.execute_tool("agent_todo_status", "{\"id\": 101}", ctx);
			std::cout << "Empty todo list result: " << res << std::endl;
			assert(res.find("No items in todo list") != std::string::npos);
		}

		// Add some todos to the subagent
		sub->add_todo("Research compiler bugs");
		sub->add_todo("Implement tests");

		// 2. Success case: populated todo list for the subagent
		{
			std::string res = registry.execute_tool("agent_todo_status", "{\"id\": 101}", ctx);
			std::cout << "Populated todo list result: " << res << std::endl;
			assert(res.find("Todo list for Agent ID 101") != std::string::npos);
			assert(res.find("- [ ] Research compiler bugs") != std::string::npos);
			assert(res.find("- [ ] Implement tests") != std::string::npos);
		}

		// 3. Execution error: subagent ID not found
		{
			std::string res = registry.execute_tool("agent_todo_status", "{\"id\": 999}", ctx);
			std::cout << "Not found subagent result: " << res << std::endl;
			assert(res.find("Error: Could not find subagent with ID 999") != std::string::npos);
		}

		// 4. Security/Validation case: reject negative subagent ID (based on review recommendations)
		{
			auto prep = registry.prepare_tool("agent_todo_status", "{\"id\": -5}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Security/Validation case: reject unexpected arguments (based on review recommendations)
		{
			auto prep = registry.prepare_tool("agent_todo_status", "{\"id\": 101, \"extra_arg\": true}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 6. Runtime validation case: reject if no active agent context
		{
			ctx.active_agent = nullptr;
			auto prep = registry.prepare_tool("agent_todo_status", "{\"id\": 101}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "agent_todo_status tool verified successfully!" << std::endl;
	}

	return 0;
}
