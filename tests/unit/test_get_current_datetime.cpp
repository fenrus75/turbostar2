#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing get_current_datetime..." << std::endl;
	{
		// 1. Success case: retrieve current date and time
		{
			std::string res = registry.execute_tool("get_current_datetime", "{}", ctx);
			std::cout << "Current datetime result: " << res << std::endl;
			assert(!res.empty());
			assert(res.find("| Unix Time |") != std::string::npos);
			assert(res.find("| Year |") != std::string::npos);
		}

		// 2. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("get_current_datetime", "{\"unexpected_arg\": 1}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "get_current_datetime tool verified successfully!" << std::endl;
	}

	return 0;
}
