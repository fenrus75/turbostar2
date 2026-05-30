#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "agentlib/interactions/user_message.h"
#include "project_manager.h"
#include "event_queue.h"
#include "tools/agent_get_output/agent_get_output.h"

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

	std::cout << "Testing agent_get_output..." << std::endl;
	{
		// 1. Spawn subagent
		auto subagent = agent->spawn_subagent("Subagent1");
		int sub_id = subagent->get_id();

		// Add some interaction history
		subagent->add_interaction(std::make_shared<interaction_user_message>("Hello subagent"));
		subagent->add_interaction(std::make_shared<interaction_user_message>("Please do work"));

		// 2. Success case with keep = true
		std::string result = registry.execute_tool("agent_get_output", std::format("{{\"id\": {}, \"keep\": true}}", sub_id), ctx);
		std::cout << "Result keep=true:\n" << result << std::endl;
		assert(result.find("Hello subagent") != std::string::npos);
		assert(result.find("Please do work") != std::string::npos);
		assert(!agent->get_subagents().empty()); // Should still be there

		// 3. Success case with keep = false (default)
		result = registry.execute_tool("agent_get_output", std::format("{{\"id\": {}}}", sub_id), ctx);
		std::cout << "Result keep=false:\n" << result << std::endl;
		assert(result.find("automatically terminated") != std::string::npos);
		assert(agent->get_subagents().empty()); // Should be removed

		// 4. Try to query a non-existent subagent
		result = registry.execute_tool("agent_get_output", "{\"id\": 9999}", ctx);
		assert(result.find("Error") != std::string::npos);

		// 5. Reject invalid negative ID
		{
			auto prep = registry.prepare_tool("agent_get_output", "{\"id\": -5}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("non-negative") != std::string::npos);
		}

		// 6. Reject missing ID
		{
			auto prep = registry.prepare_tool("agent_get_output", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("required argument") != std::string::npos);
		}

		// 7. Reject if agent is read-only and keep is false (default is false)
		{
			auto original_ro = agent->is_read_only();
			agent->set_read_only(true);

			auto prep = registry.prepare_tool("agent_get_output", std::format("{{\"id\": {}}}", sub_id), ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("read-only") != std::string::npos);

			// Directly test validate_runtime on tool with read-only
			tools::agent_get_output_tool direct_tool({sub_id, false});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);

			agent->set_read_only(original_ro);
		}

		// Cleanup threads if any
		subagent->wait_until_idle();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		std::cout << "agent_get_output tool verified successfully!" << std::endl;
	}

	return 0;
}
