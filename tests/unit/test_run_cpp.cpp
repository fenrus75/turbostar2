// Tested source file: src/tools/run_cpp/run_cpp_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include "../../src/agentlib/tool_registry.h"
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

	// 1. Success case: inline C++ code execution
	{
		nlohmann::json args = {
			{"code", "std::cout << \"Hello TurboStar run_cpp: \" << (20 + 22) << std::endl;"},
			{"std", "c++23"}
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
	}

	// 4. Success case: inline C11 code execution using gcc
	{
		nlohmann::json args = {
			{"code", "printf(\"Hello TurboStar C11: %d\\n\", 11);"},
			{"std", "c11"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 4: " << result << std::endl;
		assert(result.find("Hello TurboStar C11: 11") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 5. Success case: tmp:// file path C execution
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

	// 6. Success case: tmp:// VFS include directory
	{
		auto vfs = std::make_shared<virtual_file_system>();
		vfs->mount_buffer("tmp://my_header.h", "#define TMP_VFS_MAGIC 777\n");
		ctx.fs_security.set_vfs(vfs.get());

		nlohmann::json args = {
			{"code", "#include \"my_header.h\"\nstd::cout << \"TMP Include Magic: \" << TMP_VFS_MAGIC << std::endl;"},
			{"includes", nlohmann::json::array({"tmp://"})},
			{"std", "c++23"}
		};
		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 6: " << result << std::endl;
		assert(result.find("TMP Include Magic: 777") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	// 7. Success case: tmp:// VFS library/translation unit
	{
		auto vfs = std::make_shared<virtual_file_system>();
		vfs->mount_buffer("tmp://helper.cpp", "int compute_vfs_val() { return 888; }\n");
		ctx.fs_security.set_vfs(vfs.get());

		nlohmann::json args = {
			{"code", "#include <iostream>\nint compute_vfs_val(); int main() { std::cout << \"VFS Lib Val: \" << compute_vfs_val() << std::endl; return 0; }"},
			{"libraries", nlohmann::json::array({"tmp://helper.cpp"})},
			{"std", "c++23"}
		};

		std::string result = registry.execute_tool("run_cpp", args.dump(), ctx);
		std::cout << "Result 7: " << result << std::endl;
		assert(result.find("VFS Lib Val: 888") != std::string::npos);
		assert(result.find("[Execution: SUCCESS]") != std::string::npos);
	}

	std::cout << "run_cpp tests passed successfully.\n";
	return 0;
}




