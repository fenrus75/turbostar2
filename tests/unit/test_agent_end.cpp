#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"
#include "tools/agent_end/agent_end.h"

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

	std::cout << "Testing agent_end (end_agent)..." << std::endl;
	{
		// 1. Spawn a subagent to terminate
		auto subagent = agent->spawn_subagent("Subagent1");
		int sub_id = subagent->get_id();
		assert(!agent->get_subagents().empty());

		// 2. Success case
		std::string result = registry.execute_tool("end_agent", std::format("{{\"id\": {}}}", sub_id), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("closed successfully") != std::string::npos);
		assert(agent->get_subagents().empty());

		// 3. Try to end a non-existent subagent
		result = registry.execute_tool("end_agent", "{\"id\": 9999}", ctx);
		std::cout << "Result for non-existent: " << result << std::endl;
		assert(result.find("Error") != std::string::npos);

		// 4. Reject if argument is malformed (missing id or invalid type)
		{
			auto prep = registry.prepare_tool("end_agent", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("required argument") != std::string::npos);
		}

		// 5. Reject if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		
		auto prep = registry.prepare_tool("end_agent", "{\"id\": 123}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_end_tool direct_tool({123});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);

		// Cleanup subagent threads if any
		subagent->wait_until_idle();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		std::cout << "agent_end tool verified successfully!" << std::endl;
	}

	return 0;
}
