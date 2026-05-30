#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"
#include "tools/agent_list/agent_list.h"

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

	std::cout << "Testing agent_list (list_agents)..." << std::endl;
	{
		// 1. Success case: empty subagents
		std::string result = registry.execute_tool("list_agents", "{}", ctx);
		std::cout << "Empty result:\n" << result << std::endl;
		assert(result.find("ID") != std::string::npos);
		assert(result.find("Status") != std::string::npos);

		// 2. Success case: with subagent
		auto subagent = agent->spawn_subagent("MySubagent");
		int sub_id = subagent->get_id();

		result = registry.execute_tool("list_agents", "{}", ctx);
		std::cout << "With subagent result:\n" << result << std::endl;
		assert(result.find("MySubagent") != std::string::npos);
		assert(result.find(std::to_string(sub_id)) != std::string::npos);
		assert(result.find("Idle") != std::string::npos);

		// 3. Reject if active agent is missing
		{
			ctx.active_agent = nullptr;
			auto prep = registry.prepare_tool("list_agents", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("active agent") != std::string::npos);

			// Directly test validate_runtime
			tools::agent_list_tool direct_tool;
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("active agent") != std::string::npos);

			ctx.active_agent = agent.get();
		}


		// Cleanup threads if any
		subagent->wait_until_idle();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		std::cout << "agent_list tool verified successfully!" << std::endl;
	}

	return 0;
}
