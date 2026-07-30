#include "test_watchdog.h"
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
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "ParentAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing report_final_result..." << std::endl;

	// Test 1: Directly calling report_final_result on active_agent
	{
		assert(!agent->has_final_result());
		std::string result = registry.execute_tool("report_final_result", "{\"result\": \"Task accomplished successfully!\"}", ctx);
		std::cout << "Direct tool execution result: " << result << std::endl;
		assert(agent->has_final_result());
		assert(agent->get_final_result() == "Task accomplished successfully!");
	}

	// Test 2: Spawning a subagent and calling report_final_result on it
	{
		auto subagent = agent->spawn_subagent("subagent_final");
		int sub_id = subagent->get_id();
		tool_context sub_ctx;
		sub_ctx.active_agent = subagent.get();

		assert(!subagent->has_final_result());
		std::string report_res = registry.execute_tool("report_final_result", "{\"result\": \"Subagent final outcome!\"}", sub_ctx);
		assert(subagent->has_final_result());
		assert(subagent->get_final_result() == "Subagent final outcome!");

		// Test get_subagent_output on this subagent from the parent's context
		std::string get_out_res = registry.execute_tool("get_subagent_output", std::format("{{\"id\": {}, \"keep\": true}}", sub_id), ctx);
		std::cout << "get_subagent_output result: " << get_out_res << std::endl;
		// It should be exactly the final result string
		assert(get_out_res == "Subagent final outcome!");

		// Test wait_for_subagent message
		nlohmann::json wait_args = {{"id", sub_id}};
		std::string wait_res = registry.execute_tool("wait_for_subagent", wait_args.dump(), ctx);
		std::cout << "wait_for_subagent result: " << wait_res << std::endl;
		assert(wait_res.find("to retrieve its final result.") != std::string::npos);

		// Clean up subagent
		subagent->wait_until_idle();
	}

	// Test 3: Test exit_implicitly_on_idle flag
	{
		auto fail_model = std::make_shared<ai_model>("fail-model", "Fail Model", "http://localhost:1", "Test", 0.0, 0.0);
		auto test_agent = ai_agent::create(201, "subagent_implicit_exit", fail_model, nullptr, nullptr);
		assert(!test_agent->is_exit_implicitly_on_idle());
		test_agent->set_exit_implicitly_on_idle(true);
		assert(test_agent->is_exit_implicitly_on_idle());

		assert(!test_agent->has_final_result());

		test_agent->submit_prompt("Hello");
		test_agent->wait_until_idle();

		assert(test_agent->has_final_result());
		std::string implicit_res = test_agent->get_final_result();
		assert(!implicit_res.empty());
		assert(implicit_res.find("Error: Streaming request failed") != std::string::npos);
		std::cout << "Implicit final result on idle (error case): " << implicit_res << std::endl;

		// Assert that the agent status is now dead since it has a final result
		assert(test_agent->get_status() == agent_status::dead);
	}

	// Test 4: Verify cascading dead status to child agents
	{
		std::cout << "Testing cascading dead status..." << std::endl;
		auto parent_agent = ai_agent::create(301, "ParentCascade", model, nullptr, nullptr);
		auto child_agent1 = parent_agent->spawn_subagent("ChildCascade1");
		auto child_agent2 = child_agent1->spawn_subagent("ChildCascade2");

		assert(parent_agent->get_status() == agent_status::idle);
		assert(child_agent1->get_status() == agent_status::idle);
		assert(child_agent2->get_status() == agent_status::idle);

		// Transition parent to dead status
		parent_agent->set_status(agent_status::dead);

		// Assert that parent and all children recursively are marked dead
		assert(parent_agent->get_status() == agent_status::dead);
		assert(child_agent1->get_status() == agent_status::dead);
		assert(child_agent2->get_status() == agent_status::dead);
	}

	std::cout << "agent_report_final_result tests passed successfully.\n";
	return 0;
}

