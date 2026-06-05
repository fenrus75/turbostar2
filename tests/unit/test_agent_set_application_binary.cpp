#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/config_manager.h"
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
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing agent_set_application_binary..." << std::endl;
	{
		// 1. Success case: execute tool to set main executable
		std::string res = registry.execute_tool("agent_set_application_binary", "{\"path\": \"my_test_exe\"}", ctx);
		std::cout << "Set application binary result: " << res << std::endl;
		assert(res.find("successfully set to: my_test_exe") != std::string::npos);
		assert(config_manager::get_instance().get_main_executable() == "my_test_exe");

		// 2. Validation case: reject missing 'path' argument
		{
			auto prep = registry.prepare_tool("agent_set_application_binary", "{}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Validation case: reject empty 'path' argument
		{
			auto prep = registry.prepare_tool("agent_set_application_binary", "{\"path\": \"\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Validation case: reject unexpected arguments
		{
			auto prep = registry.prepare_tool("agent_set_application_binary", "{\"path\": \"my_test_exe\", \"extra\": 123}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "agent_set_application_binary tool verified successfully!" << std::endl;
	}

	return 0;
}
