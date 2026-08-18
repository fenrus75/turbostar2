// Tested source file: src/command_runner.cpp
#include "test_watchdog.h"

#include "git_manager.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "command_runner.h"
#include "config_manager.h"
#include "crashdump_manager.h"

namespace fs = std::filesystem;

class test_command_runner : public command_runner
{
      public:
	std::string test_build_command(const std::string &raw_command) const
	{
		return build_command(raw_command);
	}

      protected:
	void on_output_chunk(const std::string & /*chunk*/) override
	{
	}
	void on_output_line(const std::string & /*line*/) override
	{
		// Not needed for this test
	}
};

void assert_contains(const std::string &str, const std::string &substr)
{
	if (str.find(substr) == std::string::npos) {
		std::cerr << "Assertion failed: String '" << str << "' does not contain '" << substr << "'\n";
		exit(1);
	}
}

void assert_not_contains(const std::string &str, const std::string &substr)
{
	if (str.find(substr) != std::string::npos) {
		std::cerr << "Assertion failed: String '" << str << "' contains '" << substr << "'\n";
		exit(1);
	}
}

int main()
{
	test_watchdog::setup_watchdog(30, true, false);
	config_manager::get_instance().set_paranoid_mode(false);

	// Force crashdump refresh
	crashdump_manager::get_instance().refresh("18141464172954113443");

	{
		sync_command_runner runner;
		runner.apply_build_profile();
		runner.set_enable_crash_catcher(true);
		std::string output = runner.execute_and_get_output("echo hello_crash_catcher");
		std::cout << "Crash catcher output: " << output << "\n";
		assert_contains(output, "hello_crash_catcher");
	}

	{
		test_command_runner runner;
		runner.apply_default_profile();
		std::string cmd = runner.test_build_command("echo hello");
		assert_contains(cmd, "systemd-run");
		assert_contains(cmd, "-p ProtectHome=tmpfs");
		assert_contains(cmd, "-p PrivateNetwork=true");
		assert_contains(cmd, "-- bash -c 'echo hello'");
	}

	{
		test_command_runner runner;
		runner.apply_internal_profile();
		std::string cmd = runner.test_build_command("echo hello");
		// bypass_sandbox is true, and paranoid mode is false
		if (cmd != "echo hello") {
			std::cerr << "Internal profile should bypass sandbox\n";
			exit(1);
		}
	}

	{
		test_command_runner runner;
		runner.apply_build_profile();
		std::string cmd = runner.test_build_command("make");
		assert_contains(cmd, "systemd-run");
		assert_contains(cmd, "-p ProtectHome=read-only");
		assert_not_contains(cmd, "-p PrivateNetwork=true");
	}

	{
		test_command_runner runner;
		runner.apply_strict_agent_profile();
		std::string cmd = runner.test_build_command("python script.py");
		assert_contains(cmd, "systemd-run");
		assert_contains(cmd, "-p ProtectHome=tmpfs");
		assert_contains(cmd, "-p PrivateNetwork=true");
	}

	{
		test_command_runner runner;
		runner.apply_strict_agent_profile();
		runner.set_allow_display(true);

		// Set dummy environment variables to test
		setenv("DISPLAY", ":99", 1);
		setenv("XAUTHORITY", "/tmp/mock_xauth", 1);
		// Ensure a mock xauth file exists so exists() checks pass in test
		std::string mock_xauth = "/tmp/mock_xauth";
		std::ofstream f(mock_xauth);
		f.close();

		std::string cmd = runner.test_build_command("my_gui_app");
		std::cout << "Allow display command line: " << cmd << "\n";
		assert_contains(cmd, "systemd-run");
		assert_contains(cmd, "-p 'Environment=DISPLAY=:99'");
		assert_contains(cmd, "-p 'Environment=SDL_VIDEODRIVER=x11'");
		assert_contains(cmd, "-p PrivateNetwork=true");
		assert_contains(cmd, "BindReadOnlyPaths=");
		assert_contains(cmd, "'Environment=XAUTHORITY=/tmp/.Xauthority'");
		assert_contains(cmd, "mesa_shader_cache");

		std::filesystem::remove(mock_xauth);
		unsetenv("DISPLAY");
		unsetenv("XAUTHORITY");
	}

	// Test paranoid mode
	config_manager::get_instance().set_paranoid_mode(true);
	{
		test_command_runner runner;
		runner.apply_internal_profile();
		std::string cmd = runner.test_build_command("echo hello");
		// Even with internal profile, paranoid mode should force systemd-run
		assert_contains(cmd, "systemd-run");
	}

	// Test get_repository_root when in a non-git directory
	config_manager::get_instance().set_paranoid_mode(false);
	{
		auto orig_cwd = fs::current_path();
		try {
			fs::current_path(fs::temp_directory_path());
			std::string repo_root = git_manager::get_instance().get_repository_root();
			std::cout << "Non-git repo root: " << repo_root << "\n";
			assert(repo_root.empty() || fs::exists(repo_root));
			assert_not_contains(repo_root, "fatal:");
		} catch (...) {
		}
		fs::current_path(orig_cwd);
	}

	// Test extra RW/RO paths, PTY mode, project hash, and perf dir
	{
		test_command_runner runner;
		runner.apply_default_profile();
		runner.set_project_dir("/tmp/test_proj");
		runner.set_project_hash("hash12345");
		runner.set_use_pty(true);
		runner.add_extra_rw_path("/tmp/extra_rw");
		runner.add_extra_ro_path("/tmp/extra_ro");
		runner.set_crash_cookie("cookie_abc");
		runner.set_perf_dir("/tmp/perf_dir");
		runner.set_enable_crash_catcher(true);

		std::string cmd = runner.test_build_command("ls");
		assert_contains(cmd, "--pty");
		assert_contains(cmd, "turbostar-project-hash12345");
		assert_contains(cmd, "extra_rw");
		assert_contains(cmd, "extra_ro");
		assert_contains(cmd, "cookie_abc");
		assert_contains(cmd, "perf_dir");
	}

	// Test sync_command_runner execution with CRLF output and crashdumps getter
	{
		sync_command_runner runner;
		runner.apply_internal_profile();
		std::string output = runner.execute_and_get_output("printf 'line1\\r\\nline2\\r\\n'");
		assert_contains(output, "line1");
		assert_contains(output, "line2");

		assert(runner.get_new_crashdumps().empty());
	}

	// Test timeout functionality
	{
		sync_command_runner runner;
		runner.set_timeout(1);
		auto start = std::chrono::steady_clock::now();
		int exit_code = runner.execute("sleep 10");
		auto end = std::chrono::steady_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

		std::cout << "Timeout test finished in " << duration << "s with exit code " << exit_code << "\n";
		assert(runner.has_timed_out());
		assert(duration < 5); // Should have timed out in ~1s instead of waiting 10s
	}

	std::cout << "test_command_runner passed!\n";
	return 0;
}

