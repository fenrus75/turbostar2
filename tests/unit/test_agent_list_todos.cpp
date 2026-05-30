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

	std::cout << "Testing agent_list_todos..." << std::endl;
	{
		// 1. Success case with no todos
		std::string list_res = registry.execute_tool("agent_list_todos", "{}", ctx);
		std::cout << "Result: " << list_res << std::endl;
		assert(list_res.find("No items in todo list") != std::string::npos);

		// Add a todo
		agent->add_todo("Test task");

		// 2. Success case with todos
		list_res = registry.execute_tool("agent_list_todos", "{}", ctx);
		std::cout << "Result with todo: " << list_res << std::endl;
		assert(list_res.find("- [ ] Test task") != std::string::npos);

		// 3. Security/Validation case: disallow extra arguments (based on review recommendations)
		auto prep_res = registry.prepare_tool("agent_list_todos", "{\"unexpected\": 123}", ctx);
		assert(prep_res.tool == nullptr);
		assert(!prep_res.error_message.empty());

		std::cout << "agent_list_todos tool verified successfully!" << std::endl;
	}

	return 0;
}
