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

	(void)hex_highlighter_registry::get_instance();
	test_watchdog::init_plugin_environment();
	tool_registry &registry = tool_registry::get_instance();

	tool_context ctx;
	ctx.properties.active_families = {"hexedit", "x86"};

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	virtual_file_system global_vfs;
	ctx.fs_security.set_vfs(&global_vfs);

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
		std::string args = "{\"path\": \"" + elf_path + "\", \"offset\": 0, \"size\": 64}";
		auto prep = registry.prepare_tool("hexinspect", args, ctx);
		assert(prep.tool == nullptr && "hexinspect must block if hexedit family is inactive");
		assert(prep.error_message.find("Security Violation") != std::string::npos);
	}

	// 2. Test family activation constraint (active)
	ctx.is_family_active = [](const std::string &family) { return family == "hexedit"; };

	// 3. Test successful execution on ELF Magic and file header
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"offset\": 0, \"size\": 16}";
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

		// Verify structure summary was generated and written to VFS tmp file, bypassing direct inclusion
		assert(res.find("A complete structural summary of this binary") != std::string::npos);
		
		std::string tmp_physical_path = fs_utils::get_project_tmp_dir() + "/inspected_structure_mock_elf_hexinspect_bin.md";
		assert(std::filesystem::exists(tmp_physical_path));

		std::ifstream ifs_tmp(tmp_physical_path);
		std::string tmp_content((std::istreambuf_iterator<char>(ifs_tmp)), std::istreambuf_iterator<char>());
		assert(tmp_content.find("### ELF Structure Overview") != std::string::npos);
		assert(tmp_content.find("#### ELF Sections") != std::string::npos);
		assert(tmp_content.find(".shstrtab") != std::string::npos);
		ifs_tmp.close();
		std::filesystem::remove(tmp_physical_path);
	}

	// 4. Test validation of empty path
	{
		std::string args = "{\"path\": \"\"}";
		auto prep = registry.prepare_tool("hexinspect", args, ctx);
		assert(prep.tool == nullptr);
		assert(!prep.error_message.empty());
	}

	// 5. Test out of bounds offset
	{
		std::string args = "{\"path\": \"" + elf_path + "\", \"offset\": 500000, \"size\": 64}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);
		assert(res.find("Error:") != std::string::npos);
		assert(res.find("out of bounds") != std::string::npos);
	}

	// 6. Test VFS path resolution for tmp://
	{
		std::string vfs_path = "tmp://test_hexinspect_vfs.bin";
		std::string physical_vfs_path = fs_utils::get_project_tmp_dir() + "/test_hexinspect_vfs.bin";

		std::ofstream ofs_vfs(physical_vfs_path, std::ios::binary);
		assert(ofs_vfs.is_open());
		ofs_vfs.write(reinterpret_cast<const char *>(elf_bytes.data()), elf_bytes.size());
		ofs_vfs.close();

		std::string args = "{\"path\": \"" + vfs_path + "\", \"offset\": 0, \"size\": 16}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);
		assert(!res.empty());
		assert(res.find("Error:") == std::string::npos);
		assert(res.find("Binary Structure Inspection: tmp://") != std::string::npos);

		std::filesystem::remove(physical_vfs_path);
	}

	// 7. Test TAR file inspection and offset_by_name symbol resolution
	{
		std::string tar_path = "tests/unit/mock_tar_hexinspect.tar";
		std::string full_tar_path = project_root + "/" + tar_path;

		// Create mock tar bytes
		std::vector<uint8_t> tar_bytes(512 * 3, 0);
		std::strcpy(reinterpret_cast<char*>(tar_bytes.data()), "test_file.txt");
		std::strcpy(reinterpret_cast<char*>(tar_bytes.data() + 124), "00000000013");
		std::memcpy(tar_bytes.data() + 257, "ustar", 5);
		std::memcpy(tar_bytes.data() + 512, "Hello Tar!\n", 11);

		std::ofstream ofs_tar(full_tar_path, std::ios::binary);
		assert(ofs_tar.is_open());
		ofs_tar.write(reinterpret_cast<const char *>(tar_bytes.data()), tar_bytes.size());
		ofs_tar.close();

		// Inspect TAR Header
		{
			std::string args = "{\"path\": \"" + tar_path + "\", \"offset\": 0, \"size\": 16}";
			std::string res = registry.execute_tool("hexinspect", args, ctx);
			assert(res.find("TAR Header: test_file.txt") != std::string::npos);
		}

		// Inspect TAR data offset resolved by name
		{
			std::string args = "{\"path\": \"" + tar_path + "\", \"offset_by_name\": \"test_file.txt\", \"size\": 16}";
			std::string res = registry.execute_tool("hexinspect", args, ctx);
			assert(res.find("TAR File Data: test_file.txt") != std::string::npos);
		}

		std::filesystem::remove(full_tar_path);
	}

	// 8. Test real TAR file inspection and offset_by_name symbol resolution using tests/testtar.tar
	{
		std::string tar_path = fs_utils::safe_absolute("tests/testtar.tar").string();
		// Inspect real TAR header and verify the overview summary table is present
		{
			std::string args = "{\"path\": \"" + tar_path + "\", \"offset\": 0, \"size\": 16}";
			std::string res = registry.execute_tool("hexinspect", args, ctx);
			assert(res.find("TAR Header: src/main.cpp") != std::string::npos);
			assert(res.find("### Archive Contents (TAR)") != std::string::npos);
			assert(res.find("| `src/main.cpp` | `0x0` | `0x200` |") != std::string::npos);
		}

		// Inspect real TAR data offset resolved by name and verify the summary table is NOT present
		{
			std::string args = "{\"path\": \"" + tar_path + "\", \"offset_by_name\": \"src/main.cpp\", \"size\": 16}";
			std::string res = registry.execute_tool("hexinspect", args, ctx);
			assert(res.find("TAR File Data: src/main.cpp") != std::string::npos);
			assert(res.find("### Archive Contents (TAR)") == std::string::npos);
		}

		// Inspect real TAR at non-zero offset and verify the summary table is NOT present
		{
			std::string args = "{\"path\": \"" + tar_path + "\", \"offset\": 512, \"size\": 16}";
			std::string res = registry.execute_tool("hexinspect", args, ctx);
			assert(res.find("### Archive Contents (TAR)") == std::string::npos);
		}
	}

	// 9. Test large archive summary table pagination & VFS tmp file writing
	{
		std::string large_tar_path = "tests/unit/mock_large_tar.tar";
		std::string full_large_path = project_root + "/" + large_tar_path;

		std::ofstream ofs_large(full_large_path, std::ios::binary);
		assert(ofs_large.is_open());

		for (int i = 0; i < 45; ++i) {
			std::vector<uint8_t> block(512, 0);
			std::string name = std::format("file_{}.txt", i);
			std::strcpy(reinterpret_cast<char*>(block.data()), name.c_str());
			std::memcpy(block.data() + 257, "ustar", 5);
			ofs_large.write(reinterpret_cast<const char *>(block.data()), block.size());
		}
		ofs_large.close();

		std::string args = "{\"path\": \"" + large_tar_path + "\", \"offset\": 0, \"size\": 16}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);

		// Verify it contains the preview table and a note about the tmp file
		assert(res.find("### Archive Contents (TAR)") != std::string::npos);
		assert(res.find("Archive content summary was too large for direct inclusion") != std::string::npos);
		assert(res.find("tmp://archive_contents_mock_large_tar_tar.md") != std::string::npos);

		// Verify the first entry is in the preview, but not the last
		assert(res.find("`file_0.txt`") != std::string::npos);
		assert(res.find("`file_44.txt`") == std::string::npos);

		// Verify the file was written to the VFS and contains the full list
		std::string tmp_physical_path = fs_utils::get_project_tmp_dir() + "/archive_contents_mock_large_tar_tar.md";
		assert(std::filesystem::exists(tmp_physical_path));
		
		std::ifstream ifs_tmp(tmp_physical_path);
		std::string tmp_content((std::istreambuf_iterator<char>(ifs_tmp)), std::istreambuf_iterator<char>());
		assert(tmp_content.find("`file_44.txt`") != std::string::npos);
		ifs_tmp.close();

		std::filesystem::remove(full_large_path);
		std::filesystem::remove(tmp_physical_path);
	}

	// 10. Test PDF structure summary and VFS write (prefer_summary_in_tmp_only == true)
	{
		std::string pdf_path = "tests/shared-mime-info-spec.pdf";
		std::string args = "{\"path\": \"" + pdf_path + "\", \"offset\": 0, \"size\": 16}";
		std::string res = registry.execute_tool("hexinspect", args, ctx);

		// Verify structure summary was generated and written to VFS tmp file, bypassing direct inclusion
		assert(res.find("A complete structural summary of this binary") != std::string::npos);
		assert(res.find("tmp://inspected_structure_shared-mime-info-spec_pdf.md") != std::string::npos);
		assert(res.find("### PDF Structural Overview") == std::string::npos); // Bypassed inline preview

		std::string tmp_physical_path = fs_utils::get_project_tmp_dir() + "/inspected_structure_shared-mime-info-spec_pdf.md";
		assert(std::filesystem::exists(tmp_physical_path));

		std::ifstream ifs_tmp(tmp_physical_path);
		std::string tmp_content((std::istreambuf_iterator<char>(ifs_tmp)), std::istreambuf_iterator<char>());
		assert(tmp_content.find("### PDF Structural Overview") != std::string::npos);
		assert(tmp_content.find("#### File Metadata") != std::string::npos);
		assert(tmp_content.find("#### PDF Objects") != std::string::npos);
		ifs_tmp.close();
		std::filesystem::remove(tmp_physical_path);
	}

	// Clean up mock file
	std::filesystem::remove(full_elf_path);

	std::cout << "hexinspect tool tests passed successfully.\n";
	return 0;
}
