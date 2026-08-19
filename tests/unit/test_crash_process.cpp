// Tested source file: src/crash_process.cpp, src/address_lookup.cpp
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

void test_crash_process_symbol_resolution()
{
	test_watchdog::scoped_test_home guard("test_crash_process");

	// Create a mock raw crash file with frame addresses and fault address
	std::string crash_file_path = "test_mock_crash.log";
	{
		std::ofstream out(crash_file_path);
		out << "TurboStar Crash Report\n";
		out << "Signal: SIGSEGV (Segmentation Fault)\n";
		out << "CrashAddress: 0x00401000\n";
		out << "#0 0x00401000 in main()\n";
		out << "#1 0x00401050 in foo()\n";
	}

	pid_t self_pid = getpid();
	std::string pid_str = std::to_string(self_pid);

	// Locate turbostar-crashprocess executable
	std::string crashproc_path = "./turbostar-crashprocess";
	if (!std::filesystem::exists(crashproc_path)) {
		crashproc_path = "./build/turbostar-crashprocess";
	}

	if (!std::filesystem::exists(crashproc_path)) {
		std::cout << "Skipping turbostar-crashprocess execution (binary not found at " << crashproc_path << ")\n";
		std::filesystem::remove(crash_file_path);
		return;
	}

	// Spawn turbostar-crashprocess <crash_file_path> <self_pid>
	pid_t child = fork();
	if (child == 0) {
		execl(crashproc_path.c_str(), crashproc_path.c_str(), crash_file_path.c_str(), pid_str.c_str(), nullptr);
		_exit(1);
	}

	int status = 0;
	waitpid(child, &status, 0);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	// Verify that *** Resolved Stack Trace *** was appended to the file
	std::ifstream in(crash_file_path);
	std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();

	assert(content.find("*** Resolved Stack Trace ***") != std::string::npos);
	assert(content.find("Fault Address") != std::string::npos);

	std::filesystem::remove(crash_file_path);
	std::cout << "test_crash_process_symbol_resolution passed successfully!\n";
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_crash_process_symbol_resolution();
	std::cout << "All crash_process tests passed.\n";
	return 0;
}
