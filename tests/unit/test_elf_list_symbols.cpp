#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/project_manager.h"
#include "elf_test_helper.h"

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

	std::string elf_path = "tests/unit/mock_elf_list_symbols.bin";
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

	std::cout << "Testing elf_list_symbols..." << std::endl;

	// 1. Test family activation constraint (inactive by default)
	ctx.is_family_active = [](const std::string &family) { return family != "x86"; };

	{
		std::string args = "{\"path\": \"" + elf_path + "\"}";
		auto prep = registry.prepare_tool("elf_list_symbols", args, ctx);
		assert(prep.tool == nullptr && "elf_list_symbols must block if x86 family is inactive");
		assert(prep.error_message.find("Security Violation") != std::string::npos);
	}

	// 2. Test family activation constraint (active)
	ctx.is_family_active = [](const std::string &family) { return family == "x86" || family == "base"; };

	// 3. Test successful execution on mock ELF (all symbols)
	{
		std::string args = "{\"path\": \"" + elf_path + "\"}";
		std::string res = registry.execute_tool("elf_list_symbols", args, ctx);
		std::cout << "elf_list_symbols result:\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("ELF Symbol Table") != std::string::npos);
		assert(res.find("| Name | Offset/Value | Size |") != std::string::npos);
		assert(res.find("my_func") != std::string::npos);
	}

	// 4. Test successful execution with regex pattern filter (matching)
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"pattern\": \"^my_.*\"}";
		std::string res = registry.execute_tool("elf_list_symbols", args, ctx);
		std::cout << "elf_list_symbols result (matching pattern):\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("my_func") != std::string::npos);
	}

	// 5. Test successful execution with regex pattern filter (non-matching)
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"pattern\": \"^nonexistent.*\"}";
		std::string res = registry.execute_tool("elf_list_symbols", args, ctx);
		std::cout << "elf_list_symbols result (non-matching pattern):\n" << res << "\n";
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("my_func") == std::string::npos);
	}

	// 6. Test invalid regular expression pattern
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"pattern\": \"[\"}";
		std::string res = registry.execute_tool("elf_list_symbols", args, ctx);
		std::cout << "elf_list_symbols result (invalid pattern):\n" << res << "\n";
		assert(res.find("Error:") != std::string::npos);
		assert(res.find("pattern") != std::string::npos);
	}

	// 7. Test validation of empty path
	{
		std::string args = "{\"path\": \"\"}";
		auto prep = registry.prepare_tool("elf_list_symbols", args, ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// Clean up mock file
	std::filesystem::remove(full_elf_path);

	std::cout << "elf_list_symbols tool tests passed successfully.\n";
	return 0;
}
