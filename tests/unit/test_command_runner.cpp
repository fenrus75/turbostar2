#include <cassert>
#include <iostream>
#include <filesystem>
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
			std::string repo_root = command_runner::get_repository_root();
			std::cout << "Non-git repo root: " << repo_root << "\n";
			assert(fs::exists(repo_root));
			assert_not_contains(repo_root, "fatal:");
		} catch (...) {
		}
		fs::current_path(orig_cwd);
	}

	std::cout << "test_command_runner passed!\n";
	return 0;
}
