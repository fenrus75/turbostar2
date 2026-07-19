#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "agentlib/tool_registry.h"
#include "agentlib/command_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/virtual_file_system.h"
#include "fs_utils.h"
#include "pluginloader.h"
#include "project_manager.h"
#include "elf_test_helper.h"
#include "hex/hex_highlighter_registry.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	// Force initialization of singletons before plugin_loader to align shutdown lifetime
	(void)command_registry::get_instance();
	(void)skill_manager::get_instance();
	tool_registry &registry = tool_registry::get_instance();
	(void)hex_highlighter_registry::get_instance();

	// Load the dynamic plugins (including hexedit)
	auto &loader = plugin_loader::get_instance();
	loader.load_all_plugins();

	tool_context ctx;
	ctx.properties.active_families = {"hexedit", "x86"};

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::string elf_path = "tests/unit/mock_elf_hexinspect.bin";
	std::string full_elf_path = project_root + "/" + elf_path;

	// Write mock ELF file to disk
	std::vector<uint8_t> elf_bytes = elf_test::create_mock_elf();
	std::ofstream ofs(full_elf_path, std::ios::binary);
	if (!ofs.is_open()) {
		std::cerr << "Failed to open mock elf file for writing: " << full_elf_path << std::endl;
		return 1;
	}
	ofs.write(reinterpret_cast<const char *>(elf_bytes.data()), elf_bytes.size());
	ofs.close();

	std::cout << "Testing hexinspect..." << std::endl;

	// 1. Test family activation constraint (inactive by default)
	ctx.is_family_active = [](const std::string &family) { return family != "hexedit"; };

	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"start_offset\": 0, \"size\": 64}";
		auto prep = registry.prepare_tool("hexinspect", args, ctx);
		assert(prep.tool == nullptr && "hexinspect must block if hexedit family is inactive");
		assert(prep.error_message.find("Security Violation") != std::string::npos);
	}

	// 2. Test family activation constraint (active)
	ctx.is_family_active = [](const std::string &family) { return family == "hexedit"; };

	// 3. Test successful execution on ELF Magic and file header
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"start_offset\": 0, \"size\": 16}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);
		std::cout << "hexinspect result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("Binary Structure Inspection") != std::string::npos);
		assert(res.find("| **MIME Type** |") != std::string::npos);
		assert(res.find("| **Description** |") != std::string::npos);
#ifdef HAS_LIBMAGIC
		assert(res.find("application/") != std::string::npos || res.find("octet-stream") != std::string::npos);
#else
		assert(res.find("libmagic disabled") != std::string::npos);
#endif
		assert(res.find("ELF Magic") != std::string::npos || res.find("e_ident") != std::string::npos);
	}

	// 4. Test validation of empty path
	{
		std::string args = "{\"path\": \"\"}";
		auto prep = registry.prepare_tool("hexinspect", args, ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 5. Test out of bounds start_offset
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"start_offset\": 500000, \"size\": 64}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);
		assert(res.find("Error:") != std::string::npos);
		assert(res.find("out of bounds") != std::string::npos);
	}

	// 6. Test VFS path resolution for tmp://
	{
		virtual_file_system vfs;
		ctx.fs_security.set_vfs(&vfs);

		std::string vfs_path = "tmp://test_hexinspect_vfs.bin";
		std::string physical_vfs_path = fs_utils::get_project_tmp_dir() + "/test_hexinspect_vfs.bin";

		std::ofstream ofs_vfs(physical_vfs_path, std::ios::binary);
		assert(ofs_vfs.is_open());
		ofs_vfs.write(reinterpret_cast<const char *>(elf_bytes.data()), elf_bytes.size());
		ofs_vfs.close();

		std::string args = "{\"path\": \"" + vfs_path + "\", \"start_offset\": 0, \"size\": 16}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("Binary Structure Inspection: tmp://") != std::string::npos);

		std::filesystem::remove(physical_vfs_path);
		ctx.fs_security.set_vfs(nullptr);
	}

	// Clean up mock file
	std::filesystem::remove(full_elf_path);

	std::cout << "hexinspect tool tests passed successfully.\n";
	return 0;
}
