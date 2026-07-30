#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "event_queue.h"
#include "agentlib/subagent_manager.h"
#include "tools/list_subagents/list_subagents.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	subagent_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing list_subagents..." << std::endl;
	{
		// 1. Success case: lists registered subagent profiles (research, self)
		std::string result = registry.execute_tool("list_subagents", "{}", ctx);
		std::cout << "Subagent list result:\n" << result << std::endl;
		assert(result.find("Name") != std::string::npos);
		assert(result.find("Description") != std::string::npos);
		assert(result.find("research") != std::string::npos);
		assert(result.find("self") != std::string::npos);

		std::cout << "list_subagents tool verified successfully!" << std::endl;
	}

	return 0;
}
