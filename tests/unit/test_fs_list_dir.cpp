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

	std::cout << "Testing fs_list_dir..." << std::endl;
	{
		// 1. Success case: list contents of allowed directory (e.g. "src")
		{
			std::string args = "{\"path\": \"" + project_root + "/src\"}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			std::cout << "Directory list result: " << res << std::endl;
			assert(res.find("| -------- |") != std::string::npos);
			assert(res.find("main.cpp") != std::string::npos);
		}

		// 2. Stage 2 validation failure: path is not a directory (e.g. a regular file)
		{
			std::string args = "{\"path\": \"" + project_root + "/src/main.cpp\"}";
			auto prep = registry.prepare_tool("fs_list_dir", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			assert(prep.error_message.find("not a directory") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"path\": \"" + project_root + "/src\", \"unexpected_arg\": 123}";
			auto prep = registry.prepare_tool("fs_list_dir", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 4. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("fs_list_dir", "{\"path\": \"/etc\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "fs_list_dir tool verified successfully!" << std::endl;
	}

	return 0;
}
