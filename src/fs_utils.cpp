#include "fs_utils.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <format>
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
#include "project_manager.h"

namespace fs_utils
{
static std::string g_override_project_dir;

void set_override_project_dir(const std::string &path)
{
	event_logger::get_instance().log("Override project directory set to '{}'", path);
	g_override_project_dir = path;
}

std::string get_project_dir()
{
	return !g_override_project_dir.empty() ? g_override_project_dir : project_manager::get_instance().get_project_root();
}

std::filesystem::path safe_absolute(const std::filesystem::path &p)
{
	if (p.empty()) {
		return p;
	}
	try {
		return std::filesystem::absolute(p).lexically_normal();
	} catch (const std::filesystem::filesystem_error &e) {
		event_logger::get_instance().log("Filesystem error resolving absolute path for '{}': {}", p.string(), e.what());
		return p.lexically_normal();
	} catch (...) {
		event_logger::get_instance().log("Unknown error resolving absolute path for '{}'", p.string());
		return p.lexically_normal();
	}
}

bool is_binary_file(const std::string &filepath)
{
	if (filepath.empty()) {
		return false;
	}
	std::error_code ec;
	if (!std::filesystem::is_regular_file(filepath, ec)) {
		return false;
	}
	uint64_t size = std::filesystem::file_size(filepath, ec);
	if (ec || size == 0) {
		return false;
	}
	std::ifstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}
	char buffer[4096];
	file.read(buffer, std::min<size_t>(size, sizeof(buffer)));
	size_t bytes_read = file.gcount();
	for (size_t i = 0; i < bytes_read; ++i) {
		unsigned char b = static_cast<unsigned char>(buffer[i]);
		if (b == 0) {
			return true;
		}
		// Heuristic: Control characters under 32 (except tab, newline, vertical tab, form feed, carriage return, and escape)
		// indicate a binary file. DEL (127) also indicates a binary file.
		if (b < 32 && b != 9 && b != 10 && b != 11 && b != 12 && b != 13 && b != 27) {
			return true;
		}
		if (b == 127) {
			return true;
		}
	}
	return false;
}

std::string count_lines_in_file(const std::string &filepath)
{
	if (is_binary_file(filepath)) {
		return "";
	}

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
	std::string project_root = get_project_dir();
	std::hash<std::string> hasher;
	size_t hash = hasher(project_root);

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
	std::filesystem::path tmp_dir = std::filesystem::path(get_project_cache_root()) / "tmp";

	std::error_code ec;
	std::filesystem::create_directories(tmp_dir, ec);

	return tmp_dir.string();
}

std::string get_project_history_dir(const std::string &agent_name)
{
	std::filesystem::path history_dir = std::filesystem::path(get_project_cache_root()) / "history" / agent_name;

	std::error_code ec;
	std::filesystem::create_directories(history_dir, ec);

	return history_dir.string();
}

std::string get_project_dump_dir()
{
	std::filesystem::path dump_dir = std::filesystem::path(get_project_cache_root()) / "dumps";

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

bool is_shell_safe(const std::string &s, bool allow_tilde)
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
		bool is_safe = std::isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-' || c == '=' || c == ':' || c == '+' ||
			       c == ',' || c == '@';
		if (allow_tilde && c == '~') {
			is_safe = true;
		}
		if (!is_safe) {
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

std::string escape_shell_arg(const std::string &arg)
{
	std::string escaped;
	escaped.reserve(arg.size() + 10);
	escaped += '\'';
	for (char c : arg) {
		if (c == '\'') {
			escaped += "'\\''";
		} else {
			escaped += c;
		}
	}
	escaped += '\'';
	return escaped;
}

std::string unescape_string(const std::string &input)
{
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			switch (input[i + 1]) {
			case 'n': result += '\n'; break;
			case 'r': result += '\r'; break;
			case 't': result += '\t'; break;
			case '\\': result += '\\'; break;
			case '"': result += '"'; break;
			case '\'': result += '\''; break;
			default:
				result += '\\';
				result += input[i + 1];
				break;
			}
			++i;
		} else {
			result += input[i];
		}
	}
	return result;
}

std::string shorten_filename(const std::string &filepath, int max_length)
{
	if (filepath.length() <= static_cast<size_t>(max_length)) {
		return filepath;
	}
	if (max_length <= 4) {
		return filepath.substr(filepath.length() - max_length);
	}

	std::filesystem::path p(filepath);
	std::string basename = p.filename().string();

	std::vector<std::string> parts;
	for (const auto &part : p) {
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

std::string get_turbocatch_lib_path()
{
	static std::string cached_path = []() {
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::vector<std::string> search_paths = {
		    (std::filesystem::path(proj_root) / "build" / "libturbocatch.so").string(), "libturbocatch.so",
		    "/usr/lib/x86_64-linux-gnu/libturbocatch.so", "/usr/lib64/libturbocatch.so",
		    std::filesystem::absolute(std::filesystem::path("build") / "libturbocatch.so").string()};
		for (const auto &path : search_paths) {
			if (std::filesystem::exists(path)) {
				return std::filesystem::absolute(path).string();
			}
		}
		return std::filesystem::absolute(std::filesystem::path("build") / "libturbocatch.so").string();
	}();
	return cached_path;
}

std::string base64_encode(std::span<const unsigned char> data)
{
	static const char trailing_char = '=';
	static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
					   "abcdefghijklmnopqrstuvwxyz"
					   "0123456789+/";

	std::string ret;
	int i = 0;
	int j = 0;
	unsigned char char_array_3[3];
	unsigned char char_array_4[4];

	for (unsigned char byte : data) {
		char_array_3[i++] = byte;
		if (i == 3) {
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; (i < 4); i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; (j < i + 1); j++)
			ret += base64_chars[char_array_4[j]];

		while ((i++ < 3))
			ret += trailing_char;
	}

	return ret;
}

std::string base64_encode(std::string_view text)
{
	return base64_encode(std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

} // namespace fs_utils