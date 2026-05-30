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

	std::cout << "Testing get_temperature..." << std::endl;
	{
		// 1. Success case: retrieve temperature for London
		{
			std::string res = registry.execute_tool("get_temperature", "{\"location\": \"London\"}", ctx);
			std::cout << "Temperature in London: " << res << std::endl;
			assert(!res.empty());
			assert(res.find("London") != std::string::npos);
		}

		// 2. Stage 1 validation failure: reject location Mars
		{
			auto prep = registry.prepare_tool("get_temperature", "{\"location\": \"Mars\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			assert(prep.error_message.find("extraterrestrial") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject empty location
		{
			auto prep = registry.prepare_tool("get_temperature", "{\"location\": \"\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("get_temperature", "{\"location\": \"London\", \"unexpected_arg\": 123}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "get_temperature tool verified successfully!" << std::endl;
	}

	return 0;
}
