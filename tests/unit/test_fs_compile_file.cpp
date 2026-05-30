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

	std::cout << "Testing fs_compile_file..." << std::endl;
	{
		// 1. Success case: compile src/main.cpp (sync)
		{
			std::string args = "{\"path\": \"src/main.cpp\", \"async\": false}";
			std::string res = registry.execute_tool("fs_compile_file", args, ctx);
			std::cout << "Compile file sync result: " << res << std::endl;
			assert(!res.empty());
		}

		// 2. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"path\": \"src/main.cpp\", \"async\": false, \"unexpected_arg\": 123}";
			auto prep = registry.prepare_tool("fs_compile_file", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 3. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("fs_compile_file", "{\"path\": \"/etc/passwd\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "fs_compile_file tool verified successfully!" << std::endl;
	}

	return 0;
}
