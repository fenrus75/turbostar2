#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/build_error_manager.h"
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

	std::cout << "Testing fs_compile_summary..." << std::endl;
	{
		build_error_manager &err_mgr = build_error_manager::get_instance();
		err_mgr.clear();

		// Add dummy compile error
		build_error err1;
		err1.filepath = project_root + "/src/main.cpp";
		err1.line = 12;
		err1.column = 3;
		err1.message = "Syntax error";
		err1.is_warning = false;
		err_mgr.add_error(err1);

		// Add dummy compile warning
		build_error err2;
		err2.filepath = project_root + "/src/main.cpp";
		err2.line = 25;
		err2.column = 1;
		err2.message = "Unused variable";
		err2.is_warning = true;
		err_mgr.add_error(err2);

		// 1. Success case: retrieve compilation summary
		{
			std::string res = registry.execute_tool("fs_compile_summary", "{}", ctx);
			std::cout << "Compile summary result: " << res << std::endl;
			assert(!res.empty());
			assert(res.find("src/main.cpp") != std::string::npos);
			assert(res.find("| Compiler Errors |") != std::string::npos);
		}

		// 2. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("fs_compile_summary", "{\"unexpected_arg\": 1}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "fs_compile_summary tool verified successfully!" << std::endl;
	}

	build_error_manager::get_instance().clear();
	return 0;
}
