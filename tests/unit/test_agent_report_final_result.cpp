#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "ParentAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_report_final_result..." << std::endl;

	// Test 1: Directly calling agent_report_final_result on active_agent
	{
		assert(!agent->has_final_result());
		std::string result = registry.execute_tool("agent_report_final_result", "{\"result\": \"Task accomplished successfully!\"}", ctx);
		std::cout << "Direct tool execution result: " << result << std::endl;
		assert(agent->has_final_result());
		assert(agent->get_final_result() == "Task accomplished successfully!");
	}

	// Test 2: Spawning a subagent and calling agent_report_final_result on it
	{
		auto subagent = agent->spawn_subagent("subagent_final");
		int sub_id = subagent->get_id();
		tool_context sub_ctx;
		sub_ctx.active_agent = subagent.get();

		assert(!subagent->has_final_result());
		std::string report_res = registry.execute_tool("agent_report_final_result", "{\"result\": \"Subagent final outcome!\"}", sub_ctx);
		assert(subagent->has_final_result());
		assert(subagent->get_final_result() == "Subagent final outcome!");

		// Test agent_get_output on this subagent from the parent's context
		std::string get_out_res = registry.execute_tool("agent_get_output", std::format("{{\"id\": {}, \"keep\": true}}", sub_id), ctx);
		std::cout << "agent_get_output result: " << get_out_res << std::endl;
		// It should be exactly the final result string
		assert(get_out_res == "Subagent final outcome!");

		// Test wait_for_agent message
		nlohmann::json wait_args = {{"id", sub_id}};
		std::string wait_res = registry.execute_tool("wait_for_agent", wait_args.dump(), ctx);
		std::cout << "wait_for_agent result: " << wait_res << std::endl;
		assert(wait_res.find("to retrieve its final result.") != std::string::npos);

		// Clean up subagent
		subagent->wait_until_idle();
	}

	std::cout << "agent_report_final_result tests passed successfully.\n";
	return 0;
}
