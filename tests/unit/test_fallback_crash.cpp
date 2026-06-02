#include <cassert>
#include <cstdlib>
#include <iostream>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include "../../src/crash_handler.h"

int main(int argc, char **argv)
{
	// Child process code path: triggers the signal handler
	if (argc > 1 && std::string(argv[1]) == "child") {
		// Install the fallback signal handler
		crash_handler::install_fallback_handler();

		// Raise a SIGSEGV signal
		raise(SIGSEGV);

		// We should not reach here
		return 0;
	}

	// Parent process code path: forks child and captures output
	int pipefd[2];
	if (pipe(pipefd) == -1) {
		perror("pipe");
		return 1;
	}

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return 1;
	}

	if (pid == 0) {
		// Child: Redirect stdout to pipe and run child mode
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);

		char *child_argv[] = {argv[0], (char *)"child", nullptr};
		execvp(argv[0], child_argv);
		perror("execvp");
		_exit(1);
	}

	// Parent: Close write end of pipe and read child output
	close(pipefd[1]);

	std::string output;
	char buf[256];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		output += buf;
	}
	close(pipefd[0]);

	// Wait for child to exit
	int status = 0;
	waitpid(pid, &status, 0);

	std::cout << "Child exited. Status=" << status << std::endl;
	std::cout << "Captured Output:\n" << output << std::endl;

	// Verify the output matches our expectations
	assert(output.find("*** Turbostar Fallback Crash Catcher ***") != std::string::npos);
	assert(output.find("Caught signal: 11 (SIGSEGV - Segmentation Fault)") != std::string::npos);
	assert(output.find("Stack trace:") != std::string::npos);
	assert(output.find("main") != std::string::npos); // Should show main frame

	std::cout << "test_fallback_crash passed successfully!" << std::endl;
	return 0;
}
