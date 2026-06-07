#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "tools/agent_delete_todo/agent_delete_todo.h"

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

	std::cout << "Testing agent_delete_todo..." << std::endl;
	{
		// Add a todo first so we can delete it
		agent->add_todo("My deletable todo");

		// Valid todo deletion
		std::string delete_todo_res = registry.execute_tool("agent_delete_todo", "{\"text\": \"My deletable todo\"}", ctx);
		std::cout << "Result: " << delete_todo_res << std::endl;
		assert(delete_todo_res.find("deleted") != std::string::npos);

		// Test wildcard deletion
		agent->add_todo("Task 1");
		agent->add_todo("Task 2");
		std::string delete_all_res = registry.execute_tool("agent_delete_todo", "{\"text\": \"*\"}", ctx);
		assert(delete_all_res.find("deleted") != std::string::npos);
		assert(agent->get_todos().empty());

		// Rejection of empty text (validation fails)
		auto prep_delete = registry.prepare_tool("agent_delete_todo", "{\"text\": \"\"}", ctx);
		assert(prep_delete.tool == nullptr);
		assert(!prep_delete.error_message.empty());

		// Rejection of overly long text (> 1024 characters)
		std::string long_text(1025, 'a');
		prep_delete = registry.prepare_tool("agent_delete_todo", "{\"text\": \"" + long_text + "\"}", ctx);
		assert(prep_delete.tool == nullptr);
		assert(!prep_delete.error_message.empty());

		// Rejection of control characters (ESC \x1b)
		prep_delete = registry.prepare_tool("agent_delete_todo", "{\"text\": \"unsafe\\u001btodo\"}", ctx);
		assert(prep_delete.tool == nullptr);
		assert(!prep_delete.error_message.empty());

		// Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		prep_delete = registry.prepare_tool("agent_delete_todo", "{\"text\": \"Valid but RO\"}", ctx);
		assert(prep_delete.tool == nullptr);
		assert(prep_delete.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_delete_todo_tool direct_tool("Valid but RO");
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);
		std::cout << "agent_delete_todo tool verified successfully!" << std::endl;
	}

	return 0;
}
