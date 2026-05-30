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

	std::cout << "Testing code_get_references..." << std::endl;
	{
		// 1. Success path (LSP supported file type, returns "No references found." or locations)
		{
			std::string args = "{\"path\": \"" + project_root + "/src/main.cpp\", \"line\": 10, \"character\": 5}";
			std::string res = registry.execute_tool("code_get_references", args, ctx);
			std::cout << "LSP query result: " << res << std::endl;
			assert(res.find("Error:") == std::string::npos);
		}

		// 2. Failure execution path: unsupported file type
		{
			std::string args = "{\"path\": \"" + project_root + "/test.txt\", \"line\": 10, \"character\": 5}";
			std::string res = registry.execute_tool("code_get_references", args, ctx);
			std::cout << "Unsupported file type result: " << res << std::endl;
			assert(res.find("LSP is not supported") != std::string::npos);
		}

		// 3. Stage 1 validation failure: line < 1
		{
			auto prep = registry.prepare_tool("code_get_references", "{\"path\": \"src/main.cpp\", \"line\": 0, \"character\": 5}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: character < 0
		{
			auto prep = registry.prepare_tool("code_get_references", "{\"path\": \"src/main.cpp\", \"line\": 10, \"character\": -1}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 5. Stage 1 validation failure: reject unexpected properties
		{
			auto prep = registry.prepare_tool("code_get_references", "{\"path\": \"src/main.cpp\", \"line\": 10, \"character\": 5, \"extra\": true}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 6. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("code_get_references", "{\"path\": \"/etc/passwd\", \"line\": 10, \"character\": 5}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "code_get_references tool verified successfully!" << std::endl;
	}

	return 0;
}
