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

	std::cout << "test_assert_fail passed successfully!" << std::endl;
	return 0;
}
