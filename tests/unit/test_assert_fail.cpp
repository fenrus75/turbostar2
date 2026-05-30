#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include "../../src/crashdump_manager.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

extern "C" void __assert_perror_fail(int errnum, const char *file, unsigned int line, const char *function);

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
	std::cout << "Skipping test_assert_fail because it is incompatible with AddressSanitizer (ASan)." << std::endl;
	return 77;
#endif

	if (argc > 1 && std::string(argv[1]) == "child") {
		// Child process: trigger a failed assertion
		std::cout << "[Child] Running assertion failure..." << std::endl;
		assert(1 == 2);
		return 0;
	}

	if (argc > 1 && std::string(argv[1]) == "child_perror") {
		// Child process: trigger a perror assertion failure
		std::cout << "[Child] Running perror assertion failure..." << std::endl;
		__assert_perror_fail(22, "test_perror.cpp", 99, "main");
		return 0;
	}

	std::cout << "[Parent] Initializing project manager..." << std::endl;
	project_manager::get_instance().initialize();

	// Get the standard project dump directory
	fs::path test_dump_dir = fs_utils::get_project_dump_dir();
	std::string pid_str = std::to_string(getpid()); // Temporary path just to get a unique pid to clean up later

	// Get path to libturbocatch.so
	std::string lib_path = fs_utils::get_turbocatch_lib_path();
	std::cout << "[Parent] libturbocatch path: " << lib_path << std::endl;
	assert(fs::exists(lib_path));

	// Fork to run the child process with preloaded libturbocatch
	pid_t pid = fork();
	if (pid == 0) {
		// Child setup: set env variables and exec self with "child" argument
		std::string new_preload = lib_path;
		const char *old_preload = getenv("LD_PRELOAD");
		if (old_preload && *old_preload) {
			new_preload = std::string(old_preload) + ":" + lib_path;
		}
		setenv("LD_PRELOAD", new_preload.c_str(), 1);
		setenv("TURBOSTAR_DUMP_DIR", test_dump_dir.string().c_str(), 1);

		char *child_argv[] = {argv[0], (char *)"child", nullptr};
		execvp(argv[0], child_argv);

		// If exec fails
		perror("execvp");
		_exit(1);
	}

	// Parent: wait for child
	int status = 0;
	waitpid(pid, &status, 0);

	std::cout << "[Parent] Child exited with status: " << status << std::endl;

	// Verify that the crash dump folder was created
	std::string child_pid_str = std::to_string(pid);
	fs::path crash_folder = test_dump_dir / ("crash_" + child_pid_str);
	std::cout << "[Parent] Expected crash folder: " << crash_folder.string() << std::endl;
	assert(fs::exists(crash_folder));

	// Verify that assertion.txt exists
	fs::path assert_file = crash_folder / "assertion.txt";
	assert(fs::exists(assert_file));

	// Read assertion.txt contents
	std::ifstream in(assert_file);
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::cout << "[Parent] assertion.txt content:\n" << content << std::endl;

	assert(content.find("Assertion: 1 == 2") != std::string::npos);
	assert(content.find("File: ") != std::string::npos);
	assert(content.find("Line: ") != std::string::npos);

	// Run crashdump_manager to parse and generate report.md
	auto &cm = crashdump_manager::get_instance();
	cm.refresh("");

	// Verify that report.md exists and contains the failed assertion details
	fs::path report_file = crash_folder / "report.md";
	assert(fs::exists(report_file));

	std::ifstream report_in(report_file);
	std::string report_content((std::istreambuf_iterator<char>(report_in)), std::istreambuf_iterator<char>());
	std::cout << "[Parent] report.md content:\n" << report_content << std::endl;

	assert(report_content.find("### Failed Assertion") != std::string::npos);
	assert(report_content.find("1 == 2") != std::string::npos);

	// Clean up only our created crash folder
	fs::remove_all(crash_folder);

	// Test 2: __assert_perror_fail test
	{
		std::cout << "\n[Parent] Testing __assert_perror_fail..." << std::endl;
		pid_t pid_p = fork();
		if (pid_p == 0) {
			std::string new_preload = lib_path;
			const char *old_preload = getenv("LD_PRELOAD");
			if (old_preload && *old_preload) {
				new_preload = std::string(old_preload) + ":" + lib_path;
			}
			setenv("LD_PRELOAD", new_preload.c_str(), 1);
			setenv("TURBOSTAR_DUMP_DIR", test_dump_dir.string().c_str(), 1);

			char *child_argv[] = {argv[0], (char *)"child_perror", nullptr};
			execvp(argv[0], child_argv);
			perror("execvp");
			_exit(1);
		}

		int status_p = 0;
		waitpid(pid_p, &status_p, 0);

		std::string child_pid_str_p = std::to_string(pid_p);
		fs::path crash_folder_p = test_dump_dir / ("crash_" + child_pid_str_p);
		assert(fs::exists(crash_folder_p));

		fs::path assert_file_p = crash_folder_p / "assertion.txt";
		assert(fs::exists(assert_file_p));

		std::ifstream in_p(assert_file_p);
		std::string content_p((std::istreambuf_iterator<char>(in_p)), std::istreambuf_iterator<char>());
		std::cout << "[Parent] perror assertion.txt content:\n" << content_p << std::endl;

		assert(content_p.find("Assertion: perror 22") != std::string::npos);
		assert(content_p.find("File: test_perror.cpp") != std::string::npos);
		assert(content_p.find("Line: 99") != std::string::npos);

		// Run crashdump_manager to parse and generate report.md
		cm.refresh("");

		fs::path report_file_p = crash_folder_p / "report.md";
		assert(fs::exists(report_file_p));

		std::ifstream report_in_p(report_file_p);
		std::string report_content_p((std::istreambuf_iterator<char>(report_in_p)), std::istreambuf_iterator<char>());
		std::cout << "[Parent] perror report.md content:\n" << report_content_p << std::endl;

		assert(report_content_p.find("### Failed Assertion") != std::string::npos);
		assert(report_content_p.find("perror 22") != std::string::npos);

		fs::remove_all(crash_folder_p);
	}

	std::cout << "test_assert_fail passed successfully!" << std::endl;
	return 0;
}
