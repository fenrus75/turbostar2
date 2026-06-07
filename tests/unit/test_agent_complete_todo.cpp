#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "tools/agent_complete_todo/agent_complete_todo.h"

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

	std::cout << "Testing agent_complete_todo..." << std::endl;
	{
		// Add a todo first so we can complete it
		agent->add_todo("My valid todo");

		// Valid todo completion
		std::string complete_todo_res = registry.execute_tool("agent_complete_todo", "{\"text\": \"My valid todo\"}", ctx);
		std::cout << "Result: " << complete_todo_res << std::endl;
		assert(complete_todo_res.find("complete") != std::string::npos);

		// Test outstanding todos reminder
		agent->add_todo("First todo");
		agent->add_todo("Second todo");
		agent->add_todo("Third todo");

		// Complete "First todo". Outstandings remaining: "Second todo" (reminder count 0), "Third todo" (reminder count 0).
		std::string res1 = registry.execute_tool("agent_complete_todo", "{\"text\": \"First todo\"}", ctx);
		std::cout << "Res 1: " << res1 << std::endl;
		// Next todo should be "Second todo"
		assert(res1.find("2 todo items remaining. Next todo item: 'Second todo'") != std::string::npos);

		// Now reminder_count for "Second todo" is 1. Let's complete "Third todo".
		// Outstandings remaining: "Second todo" (reminder count 1).
		std::string res2 = registry.execute_tool("agent_complete_todo", "{\"text\": \"Third todo\"}", ctx);
		std::cout << "Res 2: " << res2 << std::endl;
		assert(res2.find("1 todo item remaining. Next todo item: 'Second todo'") != std::string::npos);

		// Now reminder_count for "Second todo" is 2. Let's add another todo "Fourth todo" so outstanding count > 0.
		agent->add_todo("Fourth todo");

		// Now let's complete "Fourth todo". Outstandings remaining: "Second todo" (reminder count 2).
		// Since reminder_count for "Second todo" is 2, it should NOT include the next todo text!
		std::string res3 = registry.execute_tool("agent_complete_todo", "{\"text\": \"Fourth todo\"}", ctx);
		std::cout << "Res 3: " << res3 << std::endl;
		assert(res3.find("1 todo item remaining.") != std::string::npos);
		assert(res3.find("Second todo") == std::string::npos);

		// Let's add "Fifth todo" so we have outstanding todos again.
		agent->add_todo("Fifth todo");

		// Let's complete "Second todo".
		std::string res4 = registry.execute_tool("agent_complete_todo", "{\"text\": \"Second todo\"}", ctx);
		std::cout << "Res 4: " << res4 << std::endl;
		// Outstandings remaining: "Fifth todo" (reminder count 0).
		assert(res4.find("1 todo item remaining. Next todo item: 'Fifth todo'") != std::string::npos);

		// Complete remaining todos to keep test state clean
		std::string clean_res = registry.execute_tool("agent_complete_todo", "{\"text\": \"*\"}", ctx);

		// Test wildcard completion
		agent->add_todo("Task 1");
		agent->add_todo("Task 2");
		std::string complete_all_res = registry.execute_tool("agent_complete_todo", "{\"text\": \"*\"}", ctx);
		assert(complete_all_res.find("complete") != std::string::npos);
		for (const auto &todo : agent->get_todos()) {
			assert(todo.completed == true);
		}

		// Rejection of empty text (validation fails)
		auto prep_complete = registry.prepare_tool("agent_complete_todo", "{\"text\": \"\"}", ctx);
		assert(prep_complete.tool == nullptr);
		assert(!prep_complete.error_message.empty());

		// Rejection of overly long text (> 1024 characters)
		std::string long_text(1025, 'a');
		prep_complete = registry.prepare_tool("agent_complete_todo", "{\"text\": \"" + long_text + "\"}", ctx);
		assert(prep_complete.tool == nullptr);
		assert(!prep_complete.error_message.empty());

		// Rejection of control characters (ESC \x1b)
		prep_complete = registry.prepare_tool("agent_complete_todo", "{\"text\": \"unsafe\\u001btodo\"}", ctx);
		assert(prep_complete.tool == nullptr);
		assert(!prep_complete.error_message.empty());

		// Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		prep_complete = registry.prepare_tool("agent_complete_todo", "{\"text\": \"Valid but RO\"}", ctx);
		assert(prep_complete.tool == nullptr);
		assert(prep_complete.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_complete_todo_tool direct_tool("Valid but RO");
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);
		std::cout << "agent_complete_todo tool verified successfully!" << std::endl;
	}

	return 0;
}
