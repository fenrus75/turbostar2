#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"
#include "tools/agent_create/agent_create.h"

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

	std::cout << "Testing agent_create..." << std::endl;
	{
		// 1. Success case (async)
		std::string result = registry.execute_tool("create_agent",
			"{\"name\": \"sub1\", \"task\": \"Perform help\"}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("sub1") != std::string::npos);
		assert(result.find("successfully") != std::string::npos);

		// 2. Reject empty name
		{
			auto prep = registry.prepare_tool("create_agent",
				"{\"name\": \"\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("name cannot be empty") != std::string::npos);
		}

		// 3. Reject empty task and profile
		{
			auto prep = registry.prepare_tool("create_agent",
				"{\"name\": \"sub2\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("either a 'profile' or a 'task'") != std::string::npos);
		}

		// 4. Reject overly long name (> 64 characters)
		{
			std::string long_name(65, 'x');
			auto prep = registry.prepare_tool("create_agent",
				"{\"name\": \"" + long_name + "\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("name exceeds") != std::string::npos);
		}

		// 5. Reject overly long profile (> 10000 characters)
		{
			std::string long_profile(10001, 'y');
			auto prep = registry.prepare_tool("create_agent",
				"{\"name\": \"sub3\", \"profile\": \"" + long_profile + "\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Profile exceeds") != std::string::npos);
		}

		// 6. Reject control characters in name
		{
			auto prep = registry.prepare_tool("create_agent",
				"{\"name\": \"sub\\u001bname\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("unsafe control characters") != std::string::npos);
		}

		// 7. Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		auto prep = registry.prepare_tool("create_agent",
			"{\"name\": \"sub_ro\", \"task\": \"task\"}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_create_tool direct_tool({"sub_ro", "profile", "task", false});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);

		// Wait for sub1 to complete background task so we don't have race conditions on exit
		auto subagents = agent->get_subagents();
		assert(!subagents.empty());
		subagents[0]->wait_until_idle();

		std::cout << "agent_create tool verified successfully!" << std::endl;
	}

	return 0;
}
