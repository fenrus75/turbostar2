// Tested source file: src/tools/fs_list_dir/fs_list_dir_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <fstream>
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
			assert(prep.tool == nullptr); // Rejects unexpected arguments per schema
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

		// 7. Robustness: Directory containing unreadable file and broken symlink does not abort entire listing
		{
			std::filesystem::path test_dir = std::filesystem::path(project_root) / "test_unreadable_dir";
			std::filesystem::create_directories(test_dir);
			std::filesystem::path regular_file = test_dir / "a_regular.txt";
			{
				std::ofstream ofs(regular_file);
				ofs << "hello world\n";
			}
			std::filesystem::path broken_symlink = test_dir / "b_broken_link";
			std::error_code ec;
			std::filesystem::create_symlink(test_dir / "nonexistent_target.txt", broken_symlink, ec);

			std::filesystem::path unreadable_file = test_dir / "c_unreadable.txt";
			{
				std::ofstream ofs(unreadable_file);
				ofs << "secret\n";
			}
			std::filesystem::permissions(unreadable_file, std::filesystem::perms::none, std::filesystem::perm_options::replace, ec);

			std::string args = "{\"path\": \"" + test_dir.string() + "\"}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);

			// Clean up permissions so cleanup succeeds
			std::filesystem::permissions(unreadable_file, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);
			std::filesystem::remove_all(test_dir, ec);

			std::cout << "Robustness test result:\n" << res << std::endl;
			assert(res.find("Error reading directory") == std::string::npos);
			assert(res.find("a_regular.txt") != std::string::npos);
			assert(res.find("b_broken_link") != std::string::npos);
			// Symlinks should not report misleading RWX permissions from target status
			assert(res.find("b_broken_link | L |  |  | RWX") == std::string::npos);
		}

		// 8. Empty directory returns empty notice
		{
			std::filesystem::path empty_dir = std::filesystem::path(project_root) / "test_empty_dir";
			std::error_code ec;
			std::filesystem::create_directories(empty_dir, ec);
			std::string args = "{\"path\": \"" + empty_dir.string() + "\"}";
			std::string res = registry.execute_tool("fs_list_dir", args, ctx);
			std::filesystem::remove(empty_dir, ec);
			assert(res.find("Directory is empty") != std::string::npos);
		}

		std::cout << "fs_list_dir tool verified successfully." << std::endl;
	}

	return 0;
}
