#include <cassert>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
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

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "ParentAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	// Create subagent
	std::string create_res = registry.execute_tool("create_agent", "{\"name\": \"sub_wait\", \"task\": \"Perform task\"}", ctx);
	assert(create_res.find("successfully") != std::string::npos);

	auto subagents = agent->get_subagents();
	assert(!subagents.empty());
	int sub_id = subagents[0]->get_id();

	std::cout << "Testing wait_for_agent..." << std::endl;

	// 1. Success case: wait for the subagent
	{
		nlohmann::json args = {{"id", sub_id}};
		std::string result = registry.execute_tool("wait_for_agent", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("reached idle state") != std::string::npos || result.find("finished with") != std::string::npos);
	}

	// 2. Failure case: subagent not found
	{
		nlohmann::json args = {{"id", 99999}};
		std::string result = registry.execute_tool("wait_for_agent", args.dump(), ctx);
		std::cout << "Result not found: " << result << std::endl;
		assert(result.find("Could not find subagent") != std::string::npos);
	}

	// 3. Validation failure: missing active agent
	{
		tool_context null_ctx;
		nlohmann::json args = {{"id", sub_id}};
		auto prep = registry.prepare_tool("wait_for_agent", args.dump(), null_ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("active agent") != std::string::npos);
	}

	// 4. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"id", sub_id}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("wait_for_agent", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Clean up subagent background thread
	subagents[0]->wait_until_idle();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	std::cout << "wait_for_agent tests passed successfully.\n";
	return 0;
}
