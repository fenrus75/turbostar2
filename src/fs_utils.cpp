/*
 * File Split Notice:
 * Basic utility functions have been factored out into dedicated modules:
 *   - src/fs_basics.h / src/fs_basics.cpp:
 *       Pure utility primitives without deep Turbostar subsystems dependencies:
 *       base64 encoding/decoding, binary buffer detection, string escaping/unescaping,
 *       JSON escaping and repair, prompt XML tag wrapping, Levenshtein edit distance,
 *       UI-safe string validation, and thread naming primitives.
 *
 *   - src/fs_utils.h / src/fs_utils.cpp (this file):
 *       Integrated filesystem, cache directory resolution, compiler command lookup,
 *       synchronous process execution with build error parsing, and project-aware
 *       alternative file suggestions.
 */

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
#include <pthread.h>
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
		event_logger::get_instance().log_error("Filesystem error resolving absolute path for '{}': {}", p.string(), e.what());
		return p.lexically_normal();
	} catch (...) {
		event_logger::get_instance().log_error("Unknown error resolving absolute path for '{}'", p.string());
		return p.lexically_normal();
	}
}

std::string system_header_to_include_uri(std::string_view path_str)
{
	if (path_str.empty() || path_str.starts_with("include://")) {
		return std::string(path_str);
	}
	if (path_str.starts_with("include:/")) {
		return "include://" + std::string(path_str.substr(9));
	}
	if (path_str.starts_with("include:")) {
		return "include://" + std::string(path_str.substr(8));
	}


	std::filesystem::path p(path_str);
	if (p.is_relative()) {
		std::string proj_root = project_manager::get_instance().get_project_root();
		if (!proj_root.empty()) {
			p = (std::filesystem::path(proj_root) / p).lexically_normal();
		} else {
			p = p.lexically_normal();
		}
	} else {
		p = p.lexically_normal();
	}

	std::string abs_str = p.string();

	// 1. LLVM/Clang compiler internal headers: /usr/lib/llvm-<ver>/lib/clang/<ver>/include/
	if (abs_str.starts_with("/usr/lib/llvm-")) {
		size_t pos = abs_str.find("/lib/clang/");
		if (pos != std::string::npos) {
			size_t inc_pos = abs_str.find("/include/", pos);
			if (inc_pos != std::string::npos) {
				std::string rel_hdr = abs_str.substr(inc_pos + 9);
				if (!rel_hdr.empty()) {
					return "include://" + rel_hdr;
				}
			}
		}
	}

	// 2. GCC compiler internal headers: /usr/lib/gcc/<arch>/<ver>/include/ or include-fixed/
	if (abs_str.starts_with("/usr/lib/gcc/")) {
		size_t inc_pos = abs_str.find("/include-fixed/");
		if (inc_pos != std::string::npos) {
			std::string rel_hdr = abs_str.substr(inc_pos + 15);
			if (!rel_hdr.empty()) {
				return "include://" + rel_hdr;
			}
		}
		inc_pos = abs_str.find("/include/");
		if (inc_pos != std::string::npos) {
			std::string rel_hdr = abs_str.substr(inc_pos + 9);
			if (!rel_hdr.empty()) {
				return "include://" + rel_hdr;
			}
		}
	}

	// 3. /usr/include/
	if (abs_str.starts_with("/usr/include/")) {
		std::string rel = abs_str.substr(13); // trim "/usr/include/"

		// Trim target triplet prefix if present at start (e.g. x86_64-linux-gnu/)
		if (rel.starts_with("x86_64-linux-gnu/")) {
			rel = rel.substr(17);
		} else {
			size_t first_slash = rel.find('/');
			if (first_slash != std::string::npos) {
				std::string first_part = rel.substr(0, first_slash);
				if (first_part.find('-') != std::string::npos && (first_part.ends_with("-gnu") || first_part.ends_with("-linux"))) {
					rel = rel.substr(first_slash + 1);
				}
			}
		}

		// Trim c++/<ver>/ if present
		if (rel.starts_with("c++/")) {
			size_t slash1 = rel.find('/', 4);
			if (slash1 != std::string::npos) {
				rel = rel.substr(slash1 + 1);
				// Trim inner target triplet if present (e.g. x86_64-linux-gnu/)
				if (rel.starts_with("x86_64-linux-gnu/")) {
					rel = rel.substr(17);
				} else {
					size_t first_slash = rel.find('/');
					if (first_slash != std::string::npos) {
						std::string first_part = rel.substr(0, first_slash);
						if (first_part.find('-') != std::string::npos && (first_part.ends_with("-gnu") || first_part.ends_with("-linux"))) {
							rel = rel.substr(first_slash + 1);
						}
					}
				}
			}
		}

		if (!rel.empty()) {
			return "include://" + rel;
		}
	}


	return "";
}

std::string make_relative_to_project(std::string_view path_str, std::string_view working_dir)
{
	if (path_str.empty()) {
		return std::string(path_str);
	}
	if (path_str.starts_with("file://")) {
		path_str.remove_prefix(7);
	}

	std::string inc_uri = system_header_to_include_uri(path_str);
	if (!inc_uri.empty()) {
		return inc_uri;
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
	if (roots.empty()) {
		std::error_code ec;
		std::string cwd = std::filesystem::current_path(ec).string();
		if (!ec && !cwd.empty()) {
			roots.push_back(cwd);
		}
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

bool is_binary_file(std::string_view filepath, bool treat_null_as_binary)
{
	file_type_t type = get_file_type(filepath);
	if (type == file_type_t::BINARY) {
		return true;
	}
	if (treat_null_as_binary && type == file_type_t::MAYBE) {
		return true;
	}
	return false;
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

	return get_buffer_file_type(std::string_view(buffer, bytes_read));
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

std::string get_compile_command_for_file(std::string_view filepath, std::string_view build_dir, bool syntax_only)
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
							std::string cmd = obj.get("command").string();
							if (syntax_only && !cmd.empty()) {
								if (cmd.find("-fsyntax-only") == std::string::npos) {
									cmd += " -fsyntax-only";
								}
								size_t o_pos = cmd.find(" -o ");
								if (o_pos != std::string::npos) {
									size_t next_space = cmd.find(' ', o_pos + 4);
									if (next_space != std::string::npos) {
										cmd.replace(o_pos + 4, next_space - (o_pos + 4), "/dev/null");
									} else {
										cmd.replace(o_pos + 4, std::string::npos, "/dev/null");
									}
								}
								// Strip dependency generation flags (-MF <file>, -MQ <target>, -MD, -MMD)
								auto strip_flag = [&](const std::string &flag, bool has_arg) {
									size_t pos = 0;
									while ((pos = cmd.find(flag, pos)) != std::string::npos) {
										size_t end_pos = pos + flag.length();
										if (has_arg) {
											while (end_pos < cmd.length() && cmd[end_pos] == ' ') end_pos++;
											size_t arg_end = cmd.find(' ', end_pos);
											if (arg_end == std::string::npos) end_pos = cmd.length();
											else end_pos = arg_end;
										}
										cmd.erase(pos, end_pos - pos);
									}
								};
								strip_flag(" -MF ", true);
								strip_flag(" -MQ ", true);
								strip_flag(" -MD", false);
								strip_flag(" -MMD", false);
							}
							std::string prefix = syntax_only ? "export CCACHE_DISABLE=1 && cd " : "cd ";
							return std::format("{}{} && {}", prefix, dir, cmd);
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
	runner.set_bypass_crashdump_check(true);
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
		std::string proj = get_project_dir();
		if (proj.empty()) {
			std::error_code ec;
			proj = std::filesystem::current_path(ec).string();
		}
		std::filesystem::path tmp_dir = std::filesystem::path(proj) / ".turbostar_tmp";
		std::error_code ec;
		std::filesystem::create_directories(tmp_dir, ec);
		return std::filesystem::absolute(tmp_dir, ec).lexically_normal().string();
	}

	std::filesystem::path tmp_dir = std::filesystem::path(get_project_cache_root()) / "tmp";

	std::error_code ec;
	std::filesystem::create_directories(tmp_dir, ec);

	return std::filesystem::absolute(tmp_dir, ec).lexically_normal().string();
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

std::string find_executable(std::string_view name)
{
	if (name.empty()) {
		return "";
	}
	namespace fs = std::filesystem;
	std::error_code ec;
	if (name.starts_with('/')) {
		if (fs::exists(name, ec) && access(std::string(name).c_str(), X_OK) == 0) {
			return std::string(name);
		}
		return "";
	}

	// 1. Check ~/.local/bin/ (explicit user install prefix)
	const char *home = std::getenv("HOME");
	if (home && *home) {
		fs::path local_bin = fs::path(home) / ".local" / "bin" / name;
		if (fs::exists(local_bin, ec) && access(local_bin.c_str(), X_OK) == 0) {
			return local_bin.string();
		}
	}

	// 2. Check PATH
	const char *path_env = std::getenv("PATH");
	if (path_env && *path_env) {
		std::string_view path_view(path_env);
		size_t start = 0;
		while (start < path_view.size()) {
			size_t end = path_view.find(':', start);
			if (end == std::string_view::npos) {
				end = path_view.size();
			}
			std::string_view dir = path_view.substr(start, end - start);
			if (!dir.empty()) {
				fs::path p = fs::path(dir) / name;
				if (fs::exists(p, ec) && access(p.c_str(), X_OK) == 0) {
					return p.string();
				}
			}
			start = end + 1;
		}
	}

	// 3. Fallback standard system directories
	static constexpr std::array<std::string_view, 3> fallbacks = {
		"/usr/local/bin",
		"/usr/bin",
		"/bin"
	};
	for (const auto &dir : fallbacks) {
		fs::path p = fs::path(dir) / name;
		if (fs::exists(p, ec) && access(p.c_str(), X_OK) == 0) {
			return p.string();
		}
	}

	return "";
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

bool ensure_parent_directory_exists(const std::string &target_path, std::string &note_or_warning_out)
{
	note_or_warning_out.clear();
	std::filesystem::path p(target_path);
	std::filesystem::path parent = p.parent_path();
	if (parent.empty() || std::filesystem::exists(parent)) {
		return true;
	}

	// Trace non-existent parent components to find the topmost missing directory
	std::filesystem::path curr = parent;
	std::vector<std::filesystem::path> missing_components;

	while (!curr.empty() && !std::filesystem::exists(curr)) {
		missing_components.push_back(curr);
		curr = curr.parent_path();
	}

	std::filesystem::path existing_grandparent = curr.empty() ? std::filesystem::current_path() : curr;
	std::string warnings;

	if (!missing_components.empty() && std::filesystem::exists(existing_grandparent)) {
		const std::filesystem::path &first_missing = missing_components.back();
		std::string new_dir_name = first_missing.filename().string();

		std::vector<std::string> similar_dirs;
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(existing_grandparent, ec)) {
			if (entry.is_directory(ec)) {
				std::string existing_name = entry.path().filename().string();
				if (existing_name != new_dir_name && levenshtein_distance(new_dir_name, existing_name) == 1) {
					similar_dirs.push_back(existing_name);
				}
			}
		}

		if (!similar_dirs.empty()) {
			std::string rel_new = make_relative_to_project(first_missing.string());
			for (const auto &sim : similar_dirs) {
				warnings += std::format("⚠️ Warning: Automatically created directory '{}', but similar existing directory '{}' was found at the same level (did you mean '{}'?)\n",
				                        rel_new, sim, sim);
			}
		}
	}

	std::error_code ec;
	bool created = std::filesystem::create_directories(parent, ec);
	if (!created && !std::filesystem::exists(parent)) {
		return false;
	}

	std::string rel_parent = make_relative_to_project(parent.string());
	std::string note = std::format("(Created missing parent directory: {}/)", rel_parent);

	if (!warnings.empty()) {
		note_or_warning_out = warnings + note;
	} else {
		note_or_warning_out = note;
	}

	return true;
}

std::string filename_suggest_alternative(/* untrusted */ std::string_view untrusted_filename)
{
	if (untrusted_filename.empty()) {
		return "";
	}

	std::string clean_target(untrusted_filename);
	std::replace(clean_target.begin(), clean_target.end(), '\\', '/');

	std::filesystem::path target_p(clean_target);
	std::string target_basename = target_p.filename().string();
	if (target_basename.empty() || target_basename == "." || target_basename == "..") {
		return "";
	}

	std::string proj_root = get_project_dir();
	if (proj_root.empty()) {
		return "";
	}

	std::error_code ec;
	if (!std::filesystem::exists(proj_root, ec) || !std::filesystem::is_directory(proj_root, ec)) {
		return "";
	}

	std::filesystem::path root_p(proj_root);
	root_p = root_p.lexically_normal();

	// Canonicalize attempted path relative to project
	std::string attempted_rel = make_relative_to_project(untrusted_filename);
	std::replace(attempted_rel.begin(), attempted_rel.end(), '\\', '/');
	std::filesystem::path attempted_p(attempted_rel);
	std::filesystem::path attempted_parent = attempted_p.parent_path().lexically_normal();

	// Decompose attempted parent directory into components
	std::vector<std::string> attempted_components;
	for (const auto &elem : attempted_parent) {
		std::string s = elem.string();
		if (!s.empty() && s != ".") {
			attempted_components.push_back(s);
		}
	}

	struct candidate_t {
		int common_components{0};
		size_t common_chars{0};
		int depth{0};
		size_t path_length{0};
		std::string rel_path;
	};

	std::vector<candidate_t> matches;

	try {
		for (auto it = std::filesystem::recursive_directory_iterator(root_p, std::filesystem::directory_options::skip_permission_denied, ec);
		     it != std::filesystem::recursive_directory_iterator(); ++it) {
			if (ec) {
				ec.clear();
				continue;
			}

			const auto &entry = *it;
			std::string name = entry.path().filename().string();

			if (entry.is_directory(ec)) {
				bool is_top_level = !entry.path().parent_path().has_relative_path() || entry.path().parent_path() == root_p;
				if (name.front() == '.' || name == "tmp" || name == "temp" ||
				    (is_top_level && (name == "build" || name == "builddir" || name.starts_with("build_") || name.starts_with("build-")))) {
					it.disable_recursion_pending();
				}
				continue;
			}

			if (!entry.is_regular_file(ec)) {
				continue;
			}

			if (name != target_basename) {
				continue;
			}

			auto rel = std::filesystem::relative(entry.path(), root_p, ec);
			if (ec || rel.empty() || rel.string().starts_with("..")) {
				continue;
			}

			std::string cand_rel = rel.string();
			std::replace(cand_rel.begin(), cand_rel.end(), '\\', '/');

			if (cand_rel == attempted_rel) {
				continue;
			}

			std::filesystem::path cand_p(cand_rel);
			std::filesystem::path cand_parent = cand_p.parent_path().lexically_normal();

			std::vector<std::string> cand_components;
			for (const auto &elem : cand_parent) {
				std::string s = elem.string();
				if (!s.empty() && s != ".") {
					cand_components.push_back(s);
				}
			}

			int common_comp = 0;
			size_t min_comp = std::min(attempted_components.size(), cand_components.size());
			for (size_t i = 0; i < min_comp; ++i) {
				if (attempted_components[i] == cand_components[i]) {
					common_comp++;
				} else {
					break;
				}
			}

			size_t common_ch = 0;
			size_t min_len = std::min(attempted_rel.size(), cand_rel.size());
			while (common_ch < min_len && attempted_rel[common_ch] == cand_rel[common_ch]) {
				common_ch++;
			}

			candidate_t cand;
			cand.common_components = common_comp;
			cand.common_chars = common_ch;
			cand.depth = static_cast<int>(cand_components.size());
			cand.path_length = cand_rel.size();
			cand.rel_path = std::move(cand_rel);

			matches.push_back(std::move(cand));
		}
	} catch (...) {
		// Ignore any filesystem iteration errors
	}

	if (matches.empty()) {
		return "";
	}

	std::sort(matches.begin(), matches.end(), [](const candidate_t &a, const candidate_t &b) {
		if (a.common_components != b.common_components) {
			return a.common_components > b.common_components;
		}
		if (a.common_chars != b.common_chars) {
			return a.common_chars > b.common_chars;
		}
		if (a.depth != b.depth) {
			return a.depth < b.depth;
		}
		if (a.path_length != b.path_length) {
			return a.path_length < b.path_length;
		}
		return a.rel_path < b.rel_path;
	});

	return matches.front().rel_path;
}

} // namespace fs_utils
