// Tested source file: scripts/run_clang_tidy.sh
#include "test_watchdog.h"

#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>

int main()
{
	test_watchdog::setup_watchdog(300, false);

	const char *env_enabled = std::getenv("TURBOSTAR_ENABLE_CLANG_TIDY_TEST");
#if !defined(CLANG_TIDY_TEST_ENABLED)
	bool opt_enabled = false;
#else
	bool opt_enabled = (CLANG_TIDY_TEST_ENABLED != 0);
#endif
	bool enabled = opt_enabled || (env_enabled != nullptr && (std::string(env_enabled) == "1" || std::string(env_enabled) == "true"));

	if (!enabled) {
		std::cout << "[SKIPPED] clang-tidy test disabled. Enable via Meson option (-Denable-clang-tidy-test=true) or env (TURBOSTAR_ENABLE_CLANG_TIDY_TEST=1).\n";
		return 77; // Meson skip exit code
	}

	namespace fs = std::filesystem;
	fs::path script_path = "scripts/run_clang_tidy.sh";
	if (!fs::exists(script_path)) {
		script_path = "../scripts/run_clang_tidy.sh";
	}
	if (!fs::exists(script_path)) {
		std::cerr << "Error: scripts/run_clang_tidy.sh not found.\n";
		return 1;
	}

	std::string cmd = std::format("{} --quiet", script_path.string());
	int rc = std::system(cmd.c_str());
	if (rc != 0) {
		std::cerr << std::format("clang-tidy test failed with exit code {}\n", rc);
		return WEXITSTATUS(rc) != 0 ? WEXITSTATUS(rc) : 1;
	}

	std::cout << "clang-tidy test passed successfully.\n";
	return 0;
}
