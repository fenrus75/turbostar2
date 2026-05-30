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

	std::cout << "Testing flag_as_error..." << std::endl;
	{
		build_error_manager &err_mgr = build_error_manager::get_instance();
		err_mgr.clear();

		// 1. Success case: flag a valid error
		{
			std::string args = "{\"filename\": \"src/main.cpp\", \"line\": 10, \"column\": 5, \"length\": 4, \"error_string\": \"Syntax error\", \"is_warning\": false}";
			std::string res = registry.execute_tool("flag_as_error", args, ctx);
			std::cout << "Flag error result: " << res << std::endl;
			assert(res.find("Error flagged at") != std::string::npos);
			assert(err_mgr.get_errors().size() == 1);
			assert(err_mgr.get_errors()[0].message == "Syntax error");
			assert(err_mgr.get_errors()[0].line == 9); // 1-based to 0-based
			assert(err_mgr.get_errors()[0].column == 4);
		}

		// 2. Stage 1 validation failure: reject line < 1
		{
			std::string args = "{\"filename\": \"src/main.cpp\", \"line\": 0, \"column\": 5, \"length\": 4, \"error_string\": \"Syntax error\", \"is_warning\": false}";
			auto prep = registry.prepare_tool("flag_as_error", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 3. Stage 1 validation failure: reject column < 1
		{
			std::string args = "{\"filename\": \"src/main.cpp\", \"line\": 10, \"column\": 0, \"length\": 4, \"error_string\": \"Syntax error\", \"is_warning\": false}";
			auto prep = registry.prepare_tool("flag_as_error", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: reject length < 0
		{
			std::string args = "{\"filename\": \"src/main.cpp\", \"line\": 10, \"column\": 5, \"length\": -1, \"error_string\": \"Syntax error\", \"is_warning\": false}";
			auto prep = registry.prepare_tool("flag_as_error", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"filename\": \"src/main.cpp\", \"line\": 10, \"column\": 5, \"length\": 4, \"error_string\": \"Syntax error\", \"is_warning\": false, \"unexpected_prop\": true}";
			auto prep = registry.prepare_tool("flag_as_error", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 6. Stage 2 validation failure: reject path outside workspace
		{
			std::string args = "{\"filename\": \"/etc/passwd\", \"line\": 10, \"column\": 5, \"length\": 4, \"error_string\": \"Syntax error\", \"is_warning\": false}";
			auto prep = registry.prepare_tool("flag_as_error", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "flag_as_error tool verified successfully!" << std::endl;
	}

	build_error_manager::get_instance().clear();
	return 0;
}
