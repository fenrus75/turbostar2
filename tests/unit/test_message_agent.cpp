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

	// 1. Create a subagent so we have a target to message
	std::string create_res = registry.execute_tool("create_agent",
		"{\"name\": \"sub1\", \"task\": \"Test subagent\"}", ctx);
	assert(create_res.find("successfully") != std::string::npos);

	auto subagents = agent->get_subagents();
	assert(!subagents.empty());
	int sub_id = subagents[0]->get_id();

	std::cout << "Testing message_agent..." << std::endl;

	// 2. Success case: message the subagent
	{
		nlohmann::json args = {{"id", sub_id}, {"message", "hello subagent"}};
		std::string result = registry.execute_tool("message_agent", args.dump(), ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("successfully queued") != std::string::npos);
	}

	// 3. Validation failure: empty message
	{
		nlohmann::json args = {{"id", sub_id}, {"message", ""}};
		auto prep = registry.prepare_tool("message_agent", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("empty") != std::string::npos);
	}

	// 4. Validation failure: message too long
	{
		std::string long_msg(101 * 1024, 'x'); // 101KB (limit is 100KB)
		nlohmann::json args = {{"id", sub_id}, {"message", long_msg}};
		auto prep = registry.prepare_tool("message_agent", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("exceeds maximum") != std::string::npos);
	}

	// 5. Validation failure: null active agent context
	{
		tool_context null_ctx;
		nlohmann::json args = {{"id", sub_id}, {"message", "hello"}};
		auto prep = registry.prepare_tool("message_agent", args.dump(), null_ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("active agent") != std::string::npos);
	}

	// 6. Validation failure: unexpected arguments (should fail validation as per review recommendations)
	{
		nlohmann::json args = {{"id", sub_id}, {"message", "hello"}, {"unexpected_arg", 123}};
		auto prep = registry.prepare_tool("message_agent", args.dump(), ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Wait for the subagent thread to finish/idle
	subagents[0]->wait_until_idle();
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	std::cout << "message_agent tests passed successfully.\n";
	return 0;
}
