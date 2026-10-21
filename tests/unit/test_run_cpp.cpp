// Tested source file: src/tools/run_cpp/run_cpp_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/crashdump_manager.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"



using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(60);
	project_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	std::cout << "Testing run_cpp tool..." << std::endl;

	// 1. Success case: inline C++ code execution (auto-defaults to project C++ standard)
	{
		nlohmann::json args = {
			{"code", "std::cout << \"Hello TurboStar run_cpp: \" << (20 + 22) << std::endl;"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 1: " << result << std::endl;
		assert(result.find("Hello TurboStar run_cpp: 42") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 2. Compilation error handling
	{
		nlohmann::json args = {
			{"code", "int x = ;"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 2: " << result << std::endl;
		assert(result.find("[Compilation: FAILED]") != std::string::npos);
	}

	// 3. Runtime crash catching via libturbocatch.so preloading (SIGSEGV)
	{
		nlohmann::json args = {
			{"code", "int *ptr = nullptr;\n*ptr = 1337;"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 3: " << result << std::endl;
		assert(result.find("[Execution: FAILED") != std::string::npos || result.find("SIGSEGV") != std::string::npos || result.find("Segmentation fault") != std::string::npos);
		auto dumps = crashdump_manager::get_instance().get_crashdumps();
		assert(!dumps.empty());
		std::cout << "Original binary preserved on disk for crash_id: " << dumps.back().crash_id << std::endl;
	}

	// 4. Success case: inline C11 code execution using custom cflags
	{
		nlohmann::json args = {
			{"code", R"(printf("Hello TurboStar C11: %d\n", 11);)"},
			{"cflags", nlohmann::json::array({"-std=c11"})}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 4: " << result << std::endl;
		assert(result.find("Hello TurboStar C11: 11") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 5. Success case: tmp:// file path C execution (auto-detects C language)
	{
		auto vfs = std::make_shared<virtual_file_system>();
		vfs->mount_buffer("tmp://probe_test.c", "#include <stdio.h>\nint main() { printf(\"Hello tmp VFS C: %d\\n\", 100); return 0; }\n");
		ctx.fs_security.set_vfs(vfs.get());

		nlohmann::json args = {
			{"path", "tmp://probe_test.c"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 5: " << result << std::endl;
		assert(result.find("Hello tmp VFS C: 100") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 6. Success case: tmp:// VFS include directory via cflags -I
	{
		auto vfs = std::make_shared<virtual_file_system>();
		vfs->mount_buffer("tmp://my_header.h", "#define TMP_VFS_MAGIC 777\n");
		ctx.fs_security.set_vfs(vfs.get());

		nlohmann::json args = {
			{"code", "#include \"my_header.h\"\nstd::cout << \"TMP Include Magic: \" << TMP_VFS_MAGIC << std::endl;"},
			{"cflags", nlohmann::json::array({"-Itmp://"})},
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 6: " << result << std::endl;
		assert(result.find("TMP Include Magic: 777") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 7. Success case: tmp:// VFS extra translation unit via sources
	{
		auto vfs = std::make_shared<virtual_file_system>();
		vfs->mount_buffer("tmp://helper.cpp", "int compute_vfs_val() { return 888; }\n");
		ctx.fs_security.set_vfs(vfs.get());

		nlohmann::json args = {
			{"code", "#include <iostream>\nint compute_vfs_val(); int main() { std::cout << \"VFS Lib Val: \" << compute_vfs_val() << std::endl; return 0; }"},
			{"sources", nlohmann::json::array({"tmp://helper.cpp"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 7: " << result << std::endl;
		assert(result.find("VFS Lib Val: 888") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 8. Success case: custom cflags with macros and optimization
	{
		nlohmann::json args = {
			{"code", "#include <iostream>\nint main() { std::cout << \"Macro val: \" << CUSTOM_MACRO << std::endl; return 0; }"},
			{"cflags", nlohmann::json::array({"-DCUSTOM_MACRO=1234", "-O2"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 8: " << result << std::endl;
		assert(result.find("Macro val: 1234") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 9. Success case: custom ldflags linking math library
	{
		nlohmann::json args = {
			{"code", "#include <iostream>\n#include <cmath>\nint main() { std::cout << \"Sqrt(16): \" << sqrt(16.0) << std::endl; return 0; }"},
			{"ldflags", nlohmann::json::array({"-lm"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 9: " << result << std::endl;
		assert(result.find("Sqrt(16): 4") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 10. Success case: pkg-config packages resolution (zlib)
	{
		nlohmann::json args = {
			{"code", "#include <iostream>\n#include <zlib.h>\nint main() { std::cout << \"Zlib ver: \" << zlibVersion() << std::endl; return 0; }"},
			{"packages", nlohmann::json::array({"zlib"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 10: " << result << std::endl;
		assert(result.find("Zlib ver: ") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 11. Success case: pkg_config alias resolution (sqlite3)
	{
		nlohmann::json args = {
			{"code", "#include <iostream>\n#include <sqlite3.h>\nint main() { std::cout << \"SQLite ver: \" << sqlite3_libversion() << std::endl; return 0; }"},
			{"pkg_config", nlohmann::json::array({"sqlite3"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 11: " << result << std::endl;
		assert(result.find("SQLite ver: 3.") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 12. Security rejection: unsafe package name
	{
		nlohmann::json args = {
			{"code", "int main() { return 0; }"},
			{"packages", nlohmann::json::array({"zlib; rm -rf /"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 12: " << result << std::endl;
		assert(result.find("Package name contains invalid or unsafe characters") != std::string::npos);
	}

	// 13. Failure case: nonexistent package
	{
		nlohmann::json args = {
			{"code", "int main() { return 0; }"},
			{"packages", nlohmann::json::array({"nonexistent_pkg_xyz_999"})}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 13: " << result << std::endl;
		assert(result.find("[pkg-config: FAILED]") != std::string::npos);
	}

	std::cout << "run_cpp tests passed successfully.\n";
	return 0;
}




