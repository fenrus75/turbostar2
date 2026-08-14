#pragma once
#include <string>

namespace tools {

struct shell_command_recommendation {
	bool matched{false};
	double confidence{0.0};
	std::string suggested_tool;
	std::string explanation;
};

// [NOT Signal-Safe]
// Analyzes a proposed shell command line using pattern matching to detect if a native
// specialized VFS or project tool (fs_read_lines, fs_grep_files, fs_run_tests, git_status, etc.)
// should be recommended instead. Returns recommendation details and a confidence score (0.0 to 1.0).
shell_command_recommendation evaluate_shell_command_resteer(const std::string &command_line);

} // namespace tools
