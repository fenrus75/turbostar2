#include <cassert>
#include <iostream>
#include <string>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/event_queue.h"

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

	std::cout << "Testing agent_status..." << std::endl;
	{
		// 1. Valid preparation with a positive ID
		auto prep = registry.prepare_tool("agent_status", "{\"id\": 1}", ctx);
		assert(prep.tool != nullptr);
		assert(prep.error_message.empty());

		// 2. Security case: reject negative ID (based on review recommendations)
		{
			auto prep_neg = registry.prepare_tool("agent_status", "{\"id\": -5}", ctx);
			assert(prep_neg.tool == nullptr); // This will fail initially
			assert(!prep_neg.error_message.empty());
		}

		std::cout << "agent_status tool verified successfully!" << std::endl;
	}

	return 0;
}
