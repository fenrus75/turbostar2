#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	test_watchdog::setup_watchdog(30, false, true);

	// This test verifies the watchdog's OWN __assert_fail override (it checks for the
	// "*** TEST ASSERTION FAILED ***" header on stdout). When libturbocatch.so is preloaded
	// (via LD_PRELOAD, which the crash-catcher subsystem does), its interposed __assert_fail
	// takes precedence over the executable's weak override, so this test cannot work as intended.
	// The crash-catcher's behavior is separately covered by test_assert_fail.cpp. Skip here.
	const char *preload = getenv("LD_PRELOAD");
	if (preload && *preload && std::string(preload).find("turbocatch") != std::string::npos) {
		std::cout << "Skipping test_watchdog_assert: libturbocatch.so is preloaded and "
		             "interposes __assert_fail; covered by test_assert_fail instead." << std::endl;
		return 77;
	}

	if (argc > 1 && std::string(argv[1]) == "child_assert") {
		// Trigger an assertion failure
		assert(10 == 20);
		return 0;
	}

	if (argc > 1 && std::string(argv[1]) == "child_perror") {
		// Trigger a perror assertion failure
		assert_perror(22);
		return 0;
	}

	std::cout << "Testing test_watchdog assertion hooks..." << std::endl;

	// 1. Test assert(10 == 20) in child process
	{
		int pipe_fds[2];
		if (pipe(pipe_fds) != 0) {
			perror("pipe");
			return 1;
		}

		pid_t pid = fork();
		if (pid == 0) {
			close(pipe_fds[0]);
			dup2(pipe_fds[1], STDOUT_FILENO);
			dup2(pipe_fds[1], STDERR_FILENO);
			close(pipe_fds[1]);

			char *child_argv[] = {argv[0], (char *)"child_assert", nullptr};
			execvp(argv[0], child_argv);
			_exit(1);
		}

		close(pipe_fds[1]);
		char buffer[4096] = {0};
		ssize_t bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1);
		close(pipe_fds[0]);

		int status = 0;
		waitpid(pid, &status, 0);

		std::string output(buffer, bytes_read > 0 ? bytes_read : 0);
		std::cout << "[Assert Output]:\n" << output << std::endl;

		assert(output.find("*** TEST ASSERTION FAILED ***") != std::string::npos);
		assert(output.find("Assertion:  assert(10 == 20)") != std::string::npos);
		assert(output.find("Call Stack:") != std::string::npos);
		assert(WIFSIGNALED(status) || WEXITSTATUS(status) != 0);
	}

	// 2. Test assert_perror(22) in child process
	{
		int pipe_fds[2];
		if (pipe(pipe_fds) != 0) {
			perror("pipe");
			return 1;
		}

		pid_t pid = fork();
		if (pid == 0) {
			close(pipe_fds[0]);
			dup2(pipe_fds[1], STDOUT_FILENO);
			dup2(pipe_fds[1], STDERR_FILENO);
			close(pipe_fds[1]);

			char *child_argv[] = {argv[0], (char *)"child_perror", nullptr};
			execvp(argv[0], child_argv);
			_exit(1);
		}

		close(pipe_fds[1]);
		char buffer[4096] = {0};
		ssize_t bytes_read = read(pipe_fds[0], buffer, sizeof(buffer) - 1);
		close(pipe_fds[0]);

		int status = 0;
		waitpid(pid, &status, 0);

		std::string output(buffer, bytes_read > 0 ? bytes_read : 0);
		std::cout << "[Perror Output]:\n" << output << std::endl;

		assert(output.find("*** TEST PERROR ASSERTION FAILED ***") != std::string::npos);
		assert(output.find("Errno:      22 (Invalid argument)") != std::string::npos);
		assert(output.find("Call Stack:") != std::string::npos);
		assert(WIFSIGNALED(status) || WEXITSTATUS(status) != 0);
	}

	std::cout << "test_watchdog assertion hooks verified successfully!" << std::endl;
	return 0;
}
