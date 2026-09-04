#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	auto vfs = std::make_shared<virtual_file_system>();
	ctx.fs_security.set_vfs(vfs.get());



	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();


	std::cout << "Testing fs_glob..." << std::endl;
	{
		// 1. Success case: glob src/**/*.cpp
		{
			std::string args = "{\"pattern\": \"src/a2a/*.cpp\"}";
			std::string res = registry.execute_tool("fs_glob", args, ctx);
			std::cout << "Glob result: " << res << std::endl;
			assert(res.find("src/a2a/a2a_server.cpp") != std::string::npos);
			assert(res.find("src/a2a/a2a_client.cpp") != std::string::npos);
		}

		// 2. Success case: glob docs/*.md
		{
			std::string args = "{\"pattern\": \"docs/*.md\"}";
			std::string res = registry.execute_tool("fs_glob", args, ctx);
			std::cout << "Glob docs result: " << res << std::endl;
			assert(res.find("docs/design.md") != std::string::npos);
		}

		// 3. Security failure: reject directory traversal via ".."
		{
			std::string args = "{\"pattern\": \"../**/*\"}";
			auto prep = registry.prepare_tool("fs_glob", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
			assert(prep.error_message.find("cannot contain '..' directory traversal") != std::string::npos);
		}

		// 4. Validation failure: reject unexpected properties
		{
			std::string args = "{\"pattern\": \"src/**/*.cpp\", \"extra_arg\": true}";
			auto prep = registry.prepare_tool("fs_glob", args, ctx);
			assert(prep.tool == nullptr);
			assert(!prep.error_message.empty());
		}

		// 5. Success case: glob include://std*.h
		{
			std::string args = "{\"pattern\": \"include://std*.h\"}";
			std::string res = registry.execute_tool("fs_glob", args, ctx);
			std::cout << "Glob include result:\n" << res << std::endl;
			assert(res.find("include://stdio.h") != std::string::npos || res.find("include://stdlib.h") != std::string::npos);
		}


		// 6. Success case: glob include://bits/*.h
		{
			std::string args = "{\"pattern\": \"include://bits/*.h\"}";
			std::string res = registry.execute_tool("fs_glob", args, ctx);
			std::cout << "Glob include bits result:\n" << res << std::endl;
			assert(res.find("include://bits/c++config.h") != std::string::npos);
		}

		// 7. Success case: query alias for pattern
		{
			std::string args = "{\"query\": \"docs/*.md\"}";
			std::string res = registry.execute_tool("fs_glob", args, ctx);
			std::cout << "Glob query alias result: " << res << std::endl;
			assert(res.find("docs/design.md") != std::string::npos);
		}

		std::cout << "fs_glob tool verified successfully!" << std::endl;
	}

	return 0;
}

