#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing perf_catcher via libturbocatch.so..." << std::endl;

	fs::path tmp_perf_dir = fs::current_path() / "tests" / "unit" / "tmp_perf_test";
	fs::create_directories(tmp_perf_dir);

	fs::path lib_path = fs::current_path() / "build" / "libturbocatch.so";
	if (!fs::exists(lib_path)) {
		lib_path = fs::current_path() / "libturbocatch.so";
	}

	assert(fs::exists(lib_path));

	// Spawn child process with LD_PRELOAD and TURBOSTAR_PERF_DIR set
	pid_t child_pid = fork();
	assert(child_pid != -1);

	if (child_pid == 0) {
		setenv("LD_PRELOAD", lib_path.c_str(), 1);
		setenv("TURBOSTAR_PERF_DIR", tmp_perf_dir.c_str(), 1);

		char *const args[] = {const_cast<char *>("python3"), const_cast<char *>("-c"),
				      const_cast<char *>("sum(i*i for i in range(2000000))"), nullptr};
		execvp("python3", args);
		_exit(1);
	}

	int status = 0;
	waitpid(child_pid, &status, 0);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	std::string pid_str = std::to_string(child_pid);
	fs::path samples_file = tmp_perf_dir / ("perf_samples_" + pid_str + ".dat");
	fs::path maps_file = tmp_perf_dir / ("perf_maps_" + pid_str + ".txt");

	bool found_maps = false;
	for (const auto &entry : fs::directory_iterator(tmp_perf_dir)) {
		std::string filename = entry.path().filename().string();
		std::cout << "Found file in perf dir: " << filename << " (" << entry.file_size() << " bytes)" << std::endl;
		if (filename.starts_with("perf_maps_")) {
			found_maps = true;
		}
	}

	assert(found_maps);

	// Clean up test directory
	fs::remove_all(tmp_perf_dir);

	std::cout << "perf_catcher tests passed successfully!" << std::endl;
	return 0;
}
