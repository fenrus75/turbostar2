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
	ctx.queue = &q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_set_status..." << std::endl;
	{
		// 1. Success case with normal message
		std::string result = registry.execute_tool("agent_set_status", "{\"message\": \"Building project...\"}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("updated") != std::string::npos || result.find("message") != std::string::npos);

		// 2. Validation case: reject overly long message (> 256 characters)
		{
			std::string long_msg(257, 'a');
			auto prep = registry.prepare_tool("agent_set_status", "{\"message\": \"" + long_msg + "\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Validation case: reject empty/whitespace status message
		{
			auto prep = registry.prepare_tool("agent_set_status", "{\"message\": \"   \"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Validation case: reject unsafe control characters/escape sequences
		{
			auto prep = registry.prepare_tool("agent_set_status", "{\"message\": \"unsafe\\u001bstatus\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Security case: reject status if calling agent is read-only (based on review recommendations)
		{
			auto original_ro = agent->is_read_only();
			agent->set_read_only(true);
			auto prep = registry.prepare_tool("agent_set_status", "{\"message\": \"Valid but RO\"}", ctx);
			assert(prep.tool == nullptr); // This will fail because tool is created
			assert(!prep.error_message.empty());
			agent->set_read_only(original_ro);
		}

		std::cout << "agent_set_status tool verified successfully!" << std::endl;
	}

	return 0;
}
