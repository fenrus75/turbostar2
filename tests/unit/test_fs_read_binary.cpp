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

	std::cout << "Testing fs_read_binary..." << std::endl;
	{
		std::string poem_path = "tests/unit/poem.txt";

		// 1. Success case: read first 10 bytes of poem.txt
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"start_offset\": 0, \"size\": 10}";
			std::string res = registry.execute_tool("fs_read_binary", args, ctx);
			std::cout << "Read binary result (b64): " << res << std::endl;
			assert(!res.empty());
			assert(res.find("Error:") == std::string::npos);
		}

		// Success case: read first 4 bytes of poem.txt in hex format
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"start_offset\": 0, \"size\": 4, \"format\": \"hex\"}";
			std::string res = registry.execute_tool("fs_read_binary", args, ctx);
			std::cout << "Read binary result (hex): " << res << std::endl;
			assert(!res.empty());
			assert(res.find("Error:") == std::string::npos);
			assert(res.length() == 11);
			assert(res[2] == ' ' && res[5] == ' ' && res[8] == ' ');
		}

		// 2. Execution failure case: nonexistent file
		{
			std::string args = "{\"path\": \"nonexistent_file.bin\"}";
			std::string res = registry.execute_tool("fs_read_binary", args, ctx);
			std::cout << "Nonexistent file result: " << res << std::endl;
			assert(res.find("Error:") != std::string::npos);
		}

		// 3. Execution failure case: start_offset out of bounds
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"start_offset\": 500000, \"size\": 10}";
			std::string res = registry.execute_tool("fs_read_binary", args, ctx);
			std::cout << "Offset out of bounds result: " << res << std::endl;
			assert(res.find("out of bounds") != std::string::npos);
		}

		// 4. Stage 1 validation failure: reject empty path
		{
			auto prep = registry.prepare_tool("fs_read_binary", "{\"path\": \"\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"path\": \"" + poem_path + "\", \"unexpected_arg\": 123}";
			auto prep = registry.prepare_tool("fs_read_binary", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 6. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("fs_read_binary", "{\"path\": \"/etc/passwd\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		std::cout << "fs_read_binary tool verified successfully!" << std::endl;
	}

	return 0;
}
