#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "tools/agent_add_todo/agent_add_todo.h"

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

	std::cout << "Testing agent_add_todo..." << std::endl;
	{
		// Valid todo addition
		std::string add_todo_res = registry.execute_tool("agent_add_todo", "{\"text\": \"My valid todo\"}", ctx);
		std::cout << "Result: " << add_todo_res << std::endl;
		assert(add_todo_res.find("Added todo: My valid todo") != std::string::npos);

		// Rejection of empty text (validation fails)
		auto prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection of overly long text (> 1024 characters)
		std::string long_text(1025, 'a');
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"" + long_text + "\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection of control characters (ESC \x1b)
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"unsafe\\u001btodo\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(!prep_todo.error_message.empty());

		// Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		prep_todo = registry.prepare_tool("agent_add_todo", "{\"text\": \"Valid but RO\"}", ctx);
		assert(prep_todo.tool == nullptr);
		assert(prep_todo.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_add_todo_tool direct_tool("Valid but RO");
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);
		std::cout << "agent_add_todo tool verified successfully!" << std::endl;
	}

	return 0;
}
