#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "../../src/vfs/system_vfs_provider.h"

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

		// 1b. Success case: list contents with limit and offset
		{
			std::string args = "{\"path\": \"" + project_root + "/src\", \"limit\": 2, \"offset\": 1}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			std::cout << "Directory list pagination result: " << res << std::endl;
			assert(res.find("Showing files 2 - ") != std::string::npos);
			assert(res.find("out of") != std::string::npos);
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

#ifdef HAS_LIBMAGIC
		// 5. Success case with rich_metadata enabled
		{
			std::string args = "{\"path\": \"" + project_root + "/src\", \"rich_metadata\": true}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			std::cout << "Directory list with rich_metadata: " << res << std::endl;
			assert(res.find("| Details |") != std::string::npos);
			assert(res.find("C++") != std::string::npos || res.find("text") != std::string::npos);
		}
#endif

		// 5. Success case: list virtual directory system:// and system://languages/ with rich_metadata
		{
			virtual_file_system vfs;
			auto sys_provider = std::make_shared<turbostar::system_vfs_provider>();
			vfs.register_provider("system", sys_provider);
			ctx.fs_security.set_vfs(&vfs);

			std::string root_args = "{\"path\": \"system://\", \"rich_metadata\": true}";
			std::string root_res = registry.execute_tool("fs_list_dir", root_args, ctx);
			std::cout << "VFS Root Directory list with rich_metadata:\n" << root_res << std::endl;
			assert(root_res.find("languages | D |") != std::string::npos);
			assert(root_res.find("workflows | D |") != std::string::npos);
			assert(root_res.find("tools.md | F |") != std::string::npos);
			// Verify non-zero size for tools.md in listing
			assert(root_res.find("tools.md | F | 0 | 0 |") == std::string::npos);

			std::string args = "{\"path\": \"system://languages/\", \"rich_metadata\": true}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			std::cout << "VFS Subdirectory list with rich_metadata:\n" << res << std::endl;
			assert(res.find("cpp23.md") != std::string::npos);
			assert(res.find("Read when writing or refactoring C++23 code.") != std::string::npos);

			// Test fs_file_size on VFS path
			std::string size_args = "{\"path\": \"system://agents.md\"}";
			std::string size_res = registry.execute_tool("fs_file_size", size_args, ctx);
			std::cout << "fs_file_size on system://agents.md: " << size_res << std::endl;
			assert(size_res.find("bytes") != std::string::npos);
			assert(size_res.find("0 bytes") == std::string::npos);

			// Test fs_grep_files on VFS path
			std::string grep_args = "{\"search_path\": \"system://\", \"pattern\": \"tools\"}";
			std::string grep_res = registry.execute_tool("fs_grep_files", grep_args, ctx);
			std::cout << "fs_grep_files on system:// for 'tools':\n" << grep_res << std::endl;
			assert(grep_res.find("system://tools.md") != std::string::npos);
		}

		// 6. Test passing wildcard path into fs_list_dir suggests fs_find_files with project-relative pattern
		{
			std::string args = "{\"path\": \"src/*.cpp\"}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			assert(res.find("fs_find_files(pattern='src/*.cpp')") != std::string::npos);
		}

		std::cout << "fs_list_dir tool verified successfully." << std::endl;
	}

	return 0;
}
