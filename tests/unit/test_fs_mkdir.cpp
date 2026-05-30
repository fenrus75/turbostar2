#include <cassert>
#include <iostream>
#include <filesystem>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;
namespace fs = std::filesystem;

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

	std::cout << "Testing fs_mkdir..." << std::endl;
	{
		fs::path test_dir = fs::path(project_root) / "build" / "mock_test_dir";
		fs::remove_all(test_dir);

		// 1. Success case: create a new directory
		{
			std::string args = "{\"path\": \"" + test_dir.string() + "\"}";
			std::string res = registry.execute_tool("fs_mkdir", args, ctx);
			std::cout << "Create directory result: " << res << std::endl;
			assert(res.find("Successfully created directory") != std::string::npos);
			assert(fs::exists(test_dir));
		}

		// 2. Success case: directory already exists
		{
			std::string args = "{\"path\": \"" + test_dir.string() + "\"}";
			std::string res = registry.execute_tool("fs_mkdir", args, ctx);
			std::cout << "Directory already exists result: " << res << std::endl;
			assert(res.find("already exists") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"path\": \"" + test_dir.string() + "\", \"unexpected_arg\": true}";
			auto prep = registry.prepare_tool("fs_mkdir", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 4. Stage 2 validation failure: reject path outside workspace
		{
			auto prep = registry.prepare_tool("fs_mkdir", "{\"path\": \"/etc/mock_test_dir\"}", ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// Clean up
		fs::remove_all(test_dir);
		std::cout << "fs_mkdir tool verified successfully!" << std::endl;
	}

	return 0;
}
