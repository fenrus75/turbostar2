#include "fs_utils.h"
#include <algorithm>
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

void set_override_project_dir(std::string_view path)
{
	event_logger::get_instance().log("Override project directory set to '{}'", path);
	g_override_project_dir = std::string(path);
}

std::string get_override_project_dir()
{
	return g_override_project_dir;
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
		if (p.is_relative()) {
			std::string proj = get_project_dir();
			if (!proj.empty()) {
				std::filesystem::path cand = (std::filesystem::path(proj) / p).lexically_normal();
				if (std::filesystem::exists(cand)) {
					return cand;
				}
			}
		}
		return std::filesystem::absolute(p).lexically_normal();
	} catch (const std::filesystem::filesystem_error &e) {
		event_logger::get_instance().log("Filesystem error resolving absolute path for '{}': {}", p.string(), e.what());
		return p.lexically_normal();
	} catch (...) {
		event_logger::get_instance().log("Unknown error resolving absolute path for '{}'", p.string());
		return p.lexically_normal();
	}
}

std::string make_relative_to_project(std::string_view path_str, std::string_view working_dir)
{
	if (path_str.empty()) {
		return std::string(path_str);
	}

	std::vector<std::string> roots;
	std::string proj_root = project_manager::get_instance().get_project_root();
	if (!proj_root.empty()) {
		roots.push_back(proj_root);
	}
	std::string proj_dir = get_project_dir();
	if (!proj_dir.empty()) {
		roots.push_back(proj_dir);
	}
	if (!working_dir.empty()) {
		roots.push_back(std::string(working_dir));
	}

	std::filesystem::path target_p(path_str);
	target_p = target_p.lexically_normal();

	for (const auto &root : roots) {
		std::filesystem::path root_p(root);
		root_p = root_p.lexically_normal();
		std::error_code ec;
		auto rel = std::filesystem::relative(target_p, root_p, ec);
		if (!ec && !rel.empty() && !rel.string().starts_with("..")) {
			return rel.string();
		}
	}
	return target_p.string();
}

bool is_binary_file(std::string_view filepath)
{
	return get_file_type(filepath) == file_type_t::BINARY;
}

bool is_regular_file(std::string_view filepath) noexcept
{
	if (filepath.empty()) {
		return false;
	}
	std::error_code ec;
	std::filesystem::path fpath(filepath);
	return std::filesystem::is_regular_file(fpath, ec);
}

file_type_t get_file_type(std::string_view filepath)
{
	if (filepath.empty()) {
		return file_type_t::ASCII;
	}
	std::filesystem::path fpath(filepath);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(fpath, ec)) {
		return file_type_t::ASCII;
	}
	uint64_t size = std::filesystem::file_size(fpath, ec);
	if (ec || size == 0) {
		return file_type_t::ASCII;
	}
	std::ifstream file(fpath, std::ios::binary);
	if (!file.is_open()) {
		return file_type_t::ASCII;
	}
	char buffer[4096];
	file.read(buffer, std::min<size_t>(size, sizeof(buffer)));
	size_t bytes_read = file.gcount();

	bool has_zero = false;
	bool has_other_control = false;

	for (size_t i = 0; i < bytes_read; ++i) {
		unsigned char b = static_cast<unsigned char>(buffer[i]);
		if (b == 0) {
			has_zero = true;
		} else if ((b < 32 && b != 9 && b != 10 && b != 11 && b != 12 && b != 13 && b != 27) || b == 127) {
			has_other_control = true;
		}
	}

	if (has_other_control) {
		return file_type_t::BINARY;
	}
	if (has_zero) {
		return file_type_t::MAYBE;
	}
	return file_type_t::ASCII;
}

std::string count_lines_in_file(std::string_view filepath)
{
	if (is_binary_file(filepath)) {
		return "";
	}

	std::string fpath_str(filepath);
	struct stat sb;
	if (stat(fpath_str.c_str(), &sb) == -1) {
		return "";
	}

	// Skip excessively large files (e.g., > 20MB) to prevent stalls
	if (sb.st_size > 20 * 1024 * 1024 || sb.st_size == 0) {
		return "";
	}

	int fd = open(fpath_str.c_str(), O_RDONLY);
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

std::string get_compile_command_for_file(std::string_view filepath, std::string_view build_dir)
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
			std::string target_abs = safe_absolute(std::filesystem::path(filepath)).lexically_normal().string();
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

std::string execute_command_sync(std::string_view cmd, int timeout_seconds)
{
	build_error_manager::get_instance().clear();
	sync_compile_runner runner;
	runner.apply_build_profile();
	runner.set_timeout(timeout_seconds);
	int exit_code = runner.execute(std::string(cmd) + " 2>&1");
	runner.flush();
	runner.full_output += "\nProcess exited with code " + std::to_string(exit_code) + "\n";
	return runner.full_output;
}
std::string get_global_cache_dir()
{
	const char *override_dir = std::getenv("TURBOSTAR_CACHE_DIR");
	if (override_dir) {
		return override_dir;
	}
	const char *custom_cache = std::getenv("TURBOSTAR_CACHE_DIR");
	const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
	std::filesystem::path cache_dir;
	if (custom_cache && *custom_cache) {
		cache_dir = custom_cache;
	} else if (in_testsuite && std::string(in_testsuite) == "1") {
		const char *home = std::getenv("HOME");
		if (home) {
			cache_dir = std::filesystem::path(home) / ".cache" / std::format("turbostar_test_cache_{}", getpid());
		} else {
			cache_dir = std::filesystem::temp_directory_path() / std::format("turbostar_test_cache_{}", getpid());
		}
	} else {
		const char *home = std::getenv("HOME");
		if (home) {
			cache_dir = std::filesystem::path(home) / ".cache" / "turbostar";
		} else {
			cache_dir = std::filesystem::path(".turbostar");
		}
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
	const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
	if (in_testsuite && std::string(in_testsuite) == "1") {
		std::filesystem::path tmp_dir = std::filesystem::path(get_project_dir()) / ".turbostar_tmp";
		std::error_code ec;
		std::filesystem::create_directories(tmp_dir, ec);
		return tmp_dir.string();
	}

	std::filesystem::path tmp_dir = std::filesystem::path(get_project_cache_root()) / "tmp";

	std::error_code ec;
	std::filesystem::create_directories(tmp_dir, ec);

	return tmp_dir.string();
}

std::string get_project_history_dir(std::string_view agent_name)
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

std::string get_project_perf_dir()
{
	std::filesystem::path perf_dir = std::filesystem::path(get_project_cache_root()) / "perf";

	std::error_code ec;
	std::filesystem::create_directories(perf_dir, ec);

	return perf_dir.string();
}

bool is_valid_db_name(std::string_view name) noexcept
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

bool is_shell_safe(std::string_view s, bool allow_tilde) noexcept
{
	if (s.empty())
		return false;

	// Prevent directory traversal or flag injection at the start
	if (s.find("..") != std::string_view::npos || s.front() == '-') {
		return false;
	}

	// Strict allowlist: Alphanumeric, slash, dot, underscore, hyphen, equals, colon, plus, comma, at-sign.
	// Explicity excludes: Space, quote marks, ampersand, pipe, redirect, semicolon, backtick, dollar, braces, etc.
	for (char c : s) {
		bool is_safe = std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.' || c == '_' || c == '-' || c == '=' || c == ':' || c == '+' ||
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
bool is_safe_for_ui(std::string_view s) noexcept
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

std::string escape_shell_arg(std::string_view arg)
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

std::string unescape_string(std::string_view input)
{
	std::string result;
	result.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' && i + 1 < input.size()) {
			switch (input[i + 1]) {
				case 'n':
					result += '\n';
					break;
				case 'r':
					result += '\r';
					break;
				case 't':
					result += '\t';
					break;
				case '\\':
					result += '\\';
					break;
				case '"':
					result += '"';
					break;
				case '\'':
					result += '\'';
					break;
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

std::string escape_json_string(std::string_view input, bool include_quotes)
{
	std::string escaped;
	escaped.reserve(input.size() + (include_quotes ? 16 : 14));
	if (include_quotes) {
		escaped += '"';
	}
	for (char c : input) {
		switch (c) {
			case '"':
				escaped += "\\\"";
				break;
			case '\\':
				escaped += "\\\\";
				break;
			case '\b':
				escaped += "\\b";
				break;
			case '\f':
				escaped += "\\f";
				break;
			case '\n':
				escaped += "\\n";
				break;
			case '\r':
				escaped += "\\r";
				break;
			case '\t':
				escaped += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					escaped += std::format("\\u{:04x}", static_cast<unsigned char>(c));
				} else {
					escaped += c;
				}
				break;
		}
	}
	if (include_quotes) {
		escaped += '"';
	}
	return escaped;
}

std::string wrap_prompt_untrusted_data_tag(std::string_view tag, std::string_view content)
{
	std::string safe_content(content);
	std::string close_tag = std::format("</{}>", tag);
	std::string escaped_close = std::format("&lt;/{}>", tag);

	size_t pos = 0;
	while ((pos = safe_content.find(close_tag, pos)) != std::string::npos) {
		safe_content.replace(pos, close_tag.length(), escaped_close);
		pos += escaped_close.length();
	}

	return std::format("<{}>\n{}\n</{}>", tag, safe_content, tag);
}

std::string shorten_filename(std::string_view filepath, int max_length)
{
	if (filepath.length() <= static_cast<size_t>(max_length)) {
		return std::string(filepath);
	}
	if (max_length <= 4) {
		return std::string(filepath.substr(filepath.length() - max_length));
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
		std::string full(filepath);
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
		std::vector<std::string> search_paths;

		const char *env_dir = std::getenv("TURBOSTAR_TURBOCATCH_DIR");
		if (env_dir && *env_dir) {
			search_paths.push_back((std::filesystem::path(env_dir) / "libturbocatch.so").string());
		}

#ifdef TURBOCATCH_DIR
		search_paths.push_back((std::filesystem::path(TURBOCATCH_DIR) / "libturbocatch.so").string());
#endif

		search_paths.push_back("/usr/lib/x86_64-linux-gnu/libturbocatch.so");
		search_paths.push_back("/usr/lib64/libturbocatch.so");
		search_paths.push_back("libturbocatch.so");

		std::string proj_root = project_manager::get_instance().get_project_root();
		if (!proj_root.empty()) {
			search_paths.push_back((std::filesystem::path(proj_root) / "build" / "libturbocatch.so").string());
		}
		search_paths.push_back(std::filesystem::absolute(std::filesystem::path("build") / "libturbocatch.so").string());

		for (const auto &path : search_paths) {
			if (std::filesystem::exists(path)) {
				return std::filesystem::absolute(path).string();
			}
		}

#ifdef TURBOCATCH_DIR
		return (std::filesystem::path(TURBOCATCH_DIR) / "libturbocatch.so").string();
#endif
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

std::vector<unsigned char> base64_decode(std::string_view encoded)
{
	static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
						"abcdefghijklmnopqrstuvwxyz"
						"0123456789+/";

	auto is_base64 = [](unsigned char c) -> bool { return (isalnum(c) || (c == '+') || (c == '/')); };

	size_t in_len = encoded.size();
	size_t i = 0;
	size_t j = 0;
	int in_ = 0;
	unsigned char char_array_4[4], char_array_3[3];
	std::vector<unsigned char> ret;

	while (in_len-- && (encoded[in_] != '=') && is_base64(encoded[in_])) {
		char_array_4[i++] = encoded[in_];
		in_++;
		if (i == 4) {
			for (i = 0; i < 4; i++) {
				char_array_4[i] = static_cast<unsigned char>(base64_chars.find(char_array_4[i]));
			}
			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (i = 0; (i < 3); i++) {
				ret.push_back(char_array_3[i]);
			}
			i = 0;
		}
	}

	if (i) {
		for (j = i; j < 4; j++) {
			char_array_4[j] = 0;
		}
		for (j = 0; j < 4; j++) {
			char_array_4[j] = static_cast<unsigned char>(base64_chars.find(char_array_4[j]));
		}
		char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
		char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
		char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

		for (j = 0; (j < i - 1); j++) {
			ret.push_back(char_array_3[j]);
		}
	}

	return ret;
}


std::string format_binary_output(std::span<const unsigned char> data, std::string_view format, std::string_view mime_type)
{
	if (format == "hex") {
		std::string hex_str;
		for (size_t i = 0; i < data.size(); ++i) {
			if (i > 0) {
				hex_str += " ";
			}
			hex_str += std::format("{:02x}", data[i]);
		}
		return hex_str;
	}
	return std::format("data:{};base64,{}", mime_type, base64_encode(data));
}

} // namespace fs_utils