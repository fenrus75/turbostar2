#include "shell_command_resteer.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <regex>

namespace tools {

static std::string trim_str(std::string_view s)
{
	size_t start = 0;
	while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) {
		start++;
	}
	size_t end = s.size();
	while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\n' || s[end - 1] == '\r')) {
		end--;
	}
	return std::string(s.substr(start, end - start));
}

shell_command_recommendation evaluate_shell_command_resteer(const std::string &command_line)
{
	shell_command_recommendation rec;
	std::string cmd = trim_str(command_line);

	// Strip leading "cd <path> && " or "cd <path> 2>/dev/null; " prefixes
	static const std::regex cd_prefix_regex(R"(^\s*cd\s+[^&;]+(?:\s*2>&1|\s*2>/dev/null)?\s*(?:&&|;)\s*)");
	cmd = std::regex_replace(cmd, cd_prefix_regex, "");
	cmd = trim_str(cmd);

	// 1. Test Discovery: meson test --list / ninja -t targets
	static const std::regex test_list_grep_regex(R"(^meson\s+test.*--list.*\|\s*grep\s+(?:-i\s+)?["']?([^"'\s]+)["']?)", std::regex::icase);
	static const std::regex test_list_regex(R"(^(?:meson\s+test.*--list|ninja\s+-t\s+targets))", std::regex::icase);
	std::smatch match;

	if (std::regex_search(cmd, match, test_list_grep_regex)) {
		std::string kw = match[1].str();
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_read_lines(path=\"system://project/testlist.md?search={}\")", kw);
		rec.explanation = std::format("To discover test names matching '{}', read the live system://project/testlist.md VFS file using fs_read_lines instead of running 'meson test --list' via shell.", kw);
		return rec;
	}
	if (std::regex_search(cmd, test_list_regex)) {
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = "fs_read_lines(path=\"system://project/testlist.md\")";
		rec.explanation = "To discover available test names, read the live system://project/testlist.md VFS file using fs_read_lines instead of running 'meson test --list' via shell.";
		return rec;
	}

	// 2. Unit Test Execution: meson test [options] <testname> or ./build/test_* or ninja <target>
	static const std::regex meson_test_exec_regex(R"(^meson\s+test\s+(?:--print-errorlogs\s+|-C\s+\S+\s+|--verbose\s+)*([\w_]+)$)", std::regex::icase);
	static const std::regex direct_binary_exec_regex(R"(^\./(?:build/)?(test_[\w_]+))");
	static const std::regex ninja_test_exec_regex(R"(^ninja\s+(?:-C\s+\S+\s+)*(test_[\w_]+)$)", std::regex::icase);

	if (std::regex_match(cmd, match, meson_test_exec_regex) || std::regex_match(cmd, match, direct_binary_exec_regex) || std::regex_match(cmd, match, ninja_test_exec_regex)) {
		std::string tname = match[1].str();
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_run_tests(test_names=[\"{}\"])", tname);
		rec.explanation = std::format("To run unit tests like '{}', execute them using the native 'fs_run_tests' tool instead of raw shell commands.", tname);
		return rec;
	}

	// 3. File Reading: sed -n 'X,Yp' file, head -N file
	static const std::regex sed_read_regex(R"(^sed\s+-n\s+['"](\d+),(\d+)p['"]\s+([^\s\|;]+))");
	static const std::regex head_read_regex(R"(^head\s+-(\d+)\s+([^\s\|;]+))");

	if (std::regex_match(cmd, match, sed_read_regex)) {
		std::string s_line = match[1].str();
		std::string e_line = match[2].str();
		std::string file_path = match[3].str();
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_read_lines(path=\"{}\", start_line={}, end_line={})", file_path, s_line, e_line);
		rec.explanation = std::format("To read lines from '{}', use the native 'fs_read_lines' tool instead of shell sed.", file_path);
		return rec;
	}
	if (std::regex_match(cmd, match, head_read_regex)) {
		std::string e_line = match[1].str();
		std::string file_path = match[2].str();
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_read_lines(path=\"{}\", start_line=1, end_line={})", file_path, e_line);
		rec.explanation = std::format("To read top lines from '{}', use the native 'fs_read_lines' tool instead of shell head.", file_path);
		return rec;
	}

	// 4. Code / Workspace Search: grep -rn "pat" dir, grep -n "pat" file, find . -name "pat"
	static const std::regex grep_search_regex(R"(^grep\s+-[a-zA-Z]*[rn][a-zA-Z]*\s+["']?([^"'\s]+)["']?(?:\s+([^\s\|;]+))?)", std::regex::icase);
	static const std::regex find_name_regex(R"(^find\s+([^\s]+)\s+-name\s+["']?([^"'\s]+)["']?)", std::regex::icase);

	if (std::regex_match(cmd, match, grep_search_regex)) {
		std::string pat = match[1].str();
		std::string path_opt = match[2].matched ? match[2].str() : ".";
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_grep_files(pattern=\"{}\", path=\"{}\")", pat, path_opt);
		rec.explanation = "To search workspace code, use 'fs_grep_files' instead of shell grep.";
		return rec;
	}
	if (std::regex_match(cmd, match, find_name_regex)) {
		std::string path_opt = match[1].str();
		std::string pat = match[2].str();
		rec.matched = true;
		rec.confidence = 0.95;
		rec.suggested_tool = std::format("fs_find_files(pattern=\"{}\", path=\"{}\")", pat, path_opt);
		rec.explanation = "To search for files by name, use 'fs_find_files' or 'fs_glob' instead of shell find.";
		return rec;
	}

	// 5. Git Status / Diff / Log: git status, git diff, git log, git show --stat
	static const std::regex git_status_regex(R"(^git\s+status(?:\s+.*)?$)");
	static const std::regex git_diff_regex(R"(^git\s+diff(?:\s+--stat)?(?:\s+([^\s\|;]+))?$)");
	static const std::regex git_log_regex(R"(^git\s+log(?:\s+.*)?$)");
	static const std::regex git_show_stat_regex(R"(^git\s+show\s+--stat(?:\s+.*)?$)");

	if (std::regex_match(cmd, git_status_regex)) {
		rec.matched = true;
		rec.confidence = 0.90;
		rec.suggested_tool = "git_status()";
		rec.explanation = "To check repository status, use the native 'git_status' tool instead of shell git status.";
		return rec;
	}
	if (std::regex_match(cmd, match, git_diff_regex)) {
		std::string path_arg = match[1].matched ? match[1].str() : ".";
		rec.matched = true;
		rec.confidence = 0.90;
		rec.suggested_tool = std::format("git_diff_unstaged(path=\"{}\")", path_arg);
		rec.explanation = "To view uncommitted diffs, use the native 'git_diff_unstaged' tool instead of shell git diff.";
		return rec;
	}
	if (std::regex_match(cmd, git_log_regex) || std::regex_match(cmd, git_show_stat_regex)) {
		rec.matched = true;
		rec.confidence = 0.90;
		rec.suggested_tool = "git_log(limit=10)";
		rec.explanation = "To view commit history or commit details, use the native 'git_log' tool instead of shell git log/show.";
		return rec;
	}

	return rec;
}

} // namespace tools
