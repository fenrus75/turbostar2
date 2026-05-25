#include "fs_utils.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <lsp/json/json.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "build_error_manager.h"
#include "command_runner.h"
#include "event_logger.h"
#include "gcc_log_parser.h"
#include "git_manager.h"

namespace fs_utils
{
std::filesystem::path safe_absolute(const std::filesystem::path &p)
{
	if (p.empty()) {
		return p;
	}
	try {
		return std::filesystem::absolute(p).lexically_normal();
	} catch (const std::filesystem::filesystem_error &e) {
		event_logger::get_instance().log("Filesystem error resolving absolute path for '" + p.string() + "': " + e.what());
		return p.lexically_normal();
	} catch (...) {
		event_logger::get_instance().log("Unknown error resolving absolute path for '" + p.string() + "'");
		return p.lexically_normal();
	}
}

std::string count_lines_in_file(const std::string &filepath)
{
	struct stat sb;
	if (stat(filepath.c_str(), &sb) == -1) {
		return "";
	}

	// Skip excessively large files (e.g., > 20MB) to prevent stalls
	if (sb.st_size > 20 * 1024 * 1024 || sb.st_size == 0) {
		return "";
	}

	int fd = open(filepath.c_str(), O_RDONLY);
	if (fd == -1) {
		return "";
	}

	// Map the file
	void *map = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd); // Can close immediately after mmap

	if (map == MAP_FAILED) {
		return "";
	}

	const char *data = static_cast<const char *>(map);

	// Heuristic: Check the first 4KB for null bytes to detect binary files
	size_t check_len = std::min<size_t>(sb.st_size, 4096);
	if (memchr(data, '\0', check_len) != nullptr) {
		munmap(map, sb.st_size);
		return ""; // Looks like a binary file
	}

	// Fast line counting using memchr
	size_t lines = 0;
	const char *p = data;
	const char *end = data + sb.st_size;

	while (p < end) {
		const char *next = static_cast<const char *>(memchr(p, '\n', end - p));
		if (next == nullptr) {
			break;
		}
		lines++;
		p = next + 1;
	}

	// If the file doesn't end with a newline but has content, count the last line
	if (sb.st_size > 0 && *(end - 1) != '\n') {
		lines++;
	}

	munmap(map, sb.st_size);
	return std::to_string(lines);
}

std::string get_compile_command_for_file(const std::string &filepath, const std::string &build_dir)
{
	std::filesystem::path cc_json = std::filesystem::path(build_dir) / "compile_commands.json";
	if (!std::filesystem::exists(cc_json)) {
		cc_json = std::filesystem::path("compile_commands.json");
		if (!std::filesystem::exists(cc_json)) {
			return "";
		}
	}

	std::ifstream f(cc_json);
	if (!f.is_open())
		return "";
	std::stringstream buffer;
	buffer << f.rdbuf();
	std::string json_str = buffer.str();

	try {
		lsp::json::Value val = lsp::json::parse(json_str);
		if (val.isArray()) {
			std::string target_abs = safe_absolute(filepath).lexically_normal().string();
			for (auto &entry_val : val.array()) {
				if (entry_val.isObject()) {
					auto &obj = entry_val.object();
					if (obj.contains("file") && obj.contains("command") && obj.contains("directory")) {
						std::string dir = obj.get("directory").string();
						std::string file = obj.get("file").string();
						std::string abs_path =
						    safe_absolute(std::filesystem::path(dir) / file).lexically_normal().string();
						if (abs_path == target_abs) {
							// Found it! Run the command in the directory specified
							return "cd " + dir + " && " + obj.get("command").string();
						}
					}
				}
			}
		}
	} catch (...) {
		return "";
	}
	return "";
}

class sync_compile_runner : public command_runner
{
      public:
	sync_compile_runner() : output_line_num(0)
	{
	}

	std::string full_output;
	std::string line_buffer;
	gcc_log_parser parser;
	int output_line_num;

      protected:
	void on_output_chunk(const std::string &chunk) override
	{
		line_buffer += chunk;
		size_t pos;
		while ((pos = line_buffer.find('\n')) != std::string::npos) {
			std::string line = line_buffer.substr(0, pos);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			on_output_line(line);
			line_buffer = line_buffer.substr(pos + 1);
		}
	}

	void on_output_line(const std::string &line) override
	{
		full_output += line + "\n";
		std::vector<build_error> errs;
		parser.parse_line(line, output_line_num++, errs);
		for (const auto &e : errs) {
			build_error_manager::get_instance().add_error(e);
		}
	}

      public:
	void flush()
	{
		if (!line_buffer.empty()) {
			if (line_buffer.back() == '\r')
				line_buffer.pop_back();
			on_output_line(line_buffer);
			line_buffer.clear();
		}
	}
};

std::string execute_command_sync(const std::string &cmd)
{
	build_error_manager::get_instance().clear();
	sync_compile_runner runner;
	runner.apply_build_profile();
	int exit_code = runner.execute(cmd + " 2>&1");
	runner.flush();
	runner.full_output += "\nProcess exited with code " + std::to_string(exit_code) + "\n";
	return runner.full_output;
}
std::string get_global_cache_dir()
{
	const char *home = std::getenv("HOME");
	std::filesystem::path cache_dir;
	if (home) {
		cache_dir = std::filesystem::path(home) / ".cache" / "turbostar";
	} else {
		cache_dir = std::filesystem::path(".turbostar");
	}

	std::error_code ec;
	std::filesystem::create_directories(cache_dir, ec);

	return cache_dir.string();
}

std::string get_project_cache_root()
{
	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (repo_root.empty()) {
		repo_root = std::filesystem::current_path().string();
	}

	std::hash<std::string> hasher;
	size_t hash = hasher(repo_root);

	std::filesystem::path cache_dir = std::filesystem::path(get_global_cache_dir()) / "projects" / std::to_string(hash);

	std::error_code ec;
	std::filesystem::create_directories(cache_dir, ec);

	return cache_dir.string();
}

std::string get_project_db_dir()
{
	std::filesystem::path db_dir = std::filesystem::path(get_project_cache_root()) / "dbs";

	std::error_code ec;
	std::filesystem::create_directories(db_dir, ec);

	return db_dir.string();
}
std::string get_project_tmp_dir()
{
	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (repo_root.empty()) {
		repo_root = std::filesystem::current_path().string();
	}

	std::hash<std::string> hasher;
	size_t hash = hasher(repo_root);

	const char *home = std::getenv("HOME");
	std::filesystem::path tmp_dir;
	if (home) {
		tmp_dir = std::filesystem::path(home) / ".cache" / "turbostar" / "projects" / std::to_string(hash) / "tmp";
	} else {
		tmp_dir = std::filesystem::path(".turbostar") / "projects" / std::to_string(hash) / "tmp";
	}

	std::error_code ec;
	std::filesystem::create_directories(tmp_dir, ec);

	return tmp_dir.string();
	}

	std::string get_project_history_dir(const std::string& agent_name)
	{
	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (repo_root.empty()) {
	        repo_root = std::filesystem::current_path().string();
	}

	std::hash<std::string> hasher;
	size_t hash = hasher(repo_root);

	const char *home = std::getenv("HOME");
	std::filesystem::path history_dir;
	if (home) {
	        history_dir = std::filesystem::path(home) / ".cache" / "turbostar" / "projects" / std::to_string(hash) / "history" / agent_name;
	} else {
	        history_dir = std::filesystem::path(".turbostar") / "projects" / std::to_string(hash) / "history" / agent_name;
	}

	std::error_code ec;
	std::filesystem::create_directories(history_dir, ec);

	return history_dir.string();
	}

	std::string get_project_dump_dir()
{
	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (repo_root.empty()) {
		repo_root = std::filesystem::current_path().string();
	}

	std::hash<std::string> hasher;
	size_t hash = hasher(repo_root);

	const char *home = std::getenv("HOME");
	std::filesystem::path dump_dir;
	if (home) {
		dump_dir = std::filesystem::path(home) / ".cache" / "turbostar" / "projects" / std::to_string(hash) / "dumps";
	} else {
		dump_dir = std::filesystem::path(".turbostar") / "projects" / std::to_string(hash) / "dumps";
	}

	std::error_code ec;
	std::filesystem::create_directories(dump_dir, ec);

	return dump_dir.string();
}

bool is_valid_db_name(const std::string &name)
{
	if (name.empty())
		return false;
	for (char c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

bool is_shell_safe(const std::string &s)
{
	if (s.empty())
		return false;

	// Prevent directory traversal or flag injection at the start
	if (s.find("..") != std::string::npos || s.front() == '-') {
		return false;
	}

	// Strict allowlist: Alphanumeric, slash, dot, underscore, hyphen, equals, colon, plus, comma, at-sign.
	// Explicity excludes: Space, quote marks, ampersand, pipe, redirect, semicolon, backtick, dollar, braces, etc.
	for (char c : s) {
		if (!std::isalnum(c) && c != '/' && c != '.' && c != '_' && c != '-' && c != '=' && c != ':' && c != '+' && c != ',' &&
		    c != '@') {
			return false;
		}
	}
	return true;
}

bool is_safe_for_ui(const std::string &s)
{
	for (unsigned char c : s) {
		// Reject control characters (0-31), including ESC (27)
		// Allow TAB (9), LF (10), CR (13) if we want to be more lenient,
		// but for a status line, we should probably be strict.
		if (c < 32 || c == 127) {
			return false;
		}
	}
	return true;
	}

	std::string shorten_filename(const std::string& filepath, int max_length) {
	if (filepath.length() <= static_cast<size_t>(max_length)) {
	return filepath;
	}
	if (max_length <= 4) {
	return filepath.substr(filepath.length() - max_length);
	}

	std::filesystem::path p(filepath);
	std::string basename = p.filename().string();

	std::vector<std::string> parts;
	for (const auto& part : p) {
	parts.push_back(part.string());
	}

	if (parts.size() <= 2) {
	std::string res = "...." + basename;
	return res.substr(res.length() - max_length);
	}

	std::string first_dir = parts[0];
	int start_idx = 1;
	if (first_dir == "/" && parts.size() > 1) {
	first_dir += parts[1];
	start_idx = 2;
	}

	std::string result_right = "/" + basename;
	int remaining_space = max_length - 4 - first_dir.length() - result_right.length();

	if (remaining_space < 0) {
	std::string full = filepath;
	return "...." + full.substr(full.length() - (max_length - 4));
	}

	int current_idx = parts.size() - 2;
	while (current_idx >= start_idx) {
	std::string to_add = "/" + parts[current_idx];
	if (remaining_space >= static_cast<int>(to_add.length())) {
	result_right = to_add + result_right;
	remaining_space -= to_add.length();
	current_idx--;
	} else {
	break;
	}
	}

	return first_dir + "...." + result_right;
	}

	} // namespace fs_utils