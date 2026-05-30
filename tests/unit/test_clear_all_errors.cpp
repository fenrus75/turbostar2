#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/build_error_manager.h"

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

	std::cout << "Testing clear_all_errors..." << std::endl;
	{
		build_error_manager &err_mgr = build_error_manager::get_instance();
		err_mgr.clear();

		// Add a dummy build error
		build_error err;
		err.filepath = "src/main.cpp";
		err.line = 10;
		err.column = 5;
		err.message = "Missing semicolon";
		err.is_warning = false;
		err_mgr.add_error(err);

		assert(err_mgr.get_errors().size() == 1);

		// 1. Success case: execute tool to clear errors
		std::string res = registry.execute_tool("clear_all_errors", "{}", ctx);
		std::cout << "Clear errors result: " << res << std::endl;
		assert(res.find("All errors cleared") != std::string::npos);
		assert(err_mgr.get_errors().empty());

		// 2. Validation case: reject unexpected arguments (based on review recommendations)
		{
			auto prep = registry.prepare_tool("clear_all_errors", "{\"unexpected_arg\": 123}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "clear_all_errors tool verified successfully!" << std::endl;
	}

	return 0;
}
