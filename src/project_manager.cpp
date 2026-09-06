#include "project_manager.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <re2/re2.h>
#include <regex>
#include <set>
#include <sstream>
#include "command_runner.h"
#include "config_manager.h"
#include "crashdump_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "codereview_manager.h"
#include "utf8.h"

namespace fs = std::filesystem;

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(text_range, start_y, start_x, end_y, end_x);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(lsp_manager::location_info, path, range);

namespace {

/**
 * @brief Waits for the specified duration unless interrupted by a stop_token.
 * @return true if interrupted by stop request; false if slept full duration.
 */
bool interruptible_sleep(std::stop_token stop, std::chrono::milliseconds duration)
{
	std::condition_variable_any cv;
	std::mutex dummy_mutex;
	std::unique_lock lock(dummy_mutex);
	return cv.wait_for(lock, stop, duration, [&] { return stop.stop_requested(); });
}

} // namespace

project_manager &project_manager::get_instance()
{
	static project_manager instance;
	return instance;
}

project_manager::project_manager()
{
	event_logger::get_instance(); // Force event_logger initialization first
}

project_manager::~project_manager()
{
	shutdown();
	if (inventory_thread_.joinable()) {
		inventory_thread_.join();
	}
}

/*
 * Project Root Resolution Algorithm:
 * 1. Direct environment variable or program override (`fs_utils::get_project_dir()` / `TURBOSTAR_PROJECT_ROOT`)
 *    overrides everything.
 * 2. In normal operation (when not running inside the test suite), pick the git repository root if possible.
 * 3. If not running in a git repository, default to the current working directory (`fs::current_path()`).
 * 4. When running as part of the test suite (`TURBOSTAR_IN_TESTSUITE` or `TURBOSTAR_IN_TESTS` set):
 *    - First try to resolve git repository root.
 *    - If git root is empty (e.g. isolated test process), attempt to discover the repository root by walking up parent
 *      directories looking for `src/main.cpp` and `meson.build`.
 *    - Fallback to current working directory if no project structure is found.
 */
void project_manager::initialize()
{
	std::string override_dir = fs_utils::get_override_project_dir();
	if (!override_dir.empty()) {
		project_root_ = override_dir;
	} else {
		const char *env_root = std::getenv("TURBOSTAR_PROJECT_ROOT");
		if (env_root && *env_root) {
			project_root_ = env_root;
		} else {
			const char *in_test = std::getenv("TURBOSTAR_IN_TESTSUITE");
			if (!in_test || !*in_test) {
				in_test = std::getenv("TURBOSTAR_IN_TESTS");
			}

			if (in_test && *in_test) {
				// Test suite path resolution: search upwards from current working directory for source root
				fs::path cur = fs::current_path();
				while (!cur.empty() && cur != cur.root_path()) {
					if (fs::exists(cur / "src/main.cpp") && fs::exists(cur / "meson.build")) {
						project_root_ = cur.string();
						break;
					}
					cur = cur.parent_path();
				}
				if (project_root_.empty()) {
					project_root_ = git_manager::get_instance().get_repository_root();
				}
				if (project_root_.empty()) {
					project_root_ = fs::current_path().string();
				}
			} else {
				// Normal operation (outside test suite): pick git root if possible, fallback to current directory
				project_root_ = git_manager::get_instance().get_repository_root();
				if (project_root_.empty()) {
					project_root_ = fs::current_path().string();
				}
			}
		}
	}
	initialized_ = true;

	// Load project-specific configuration overlay if available (~/.cache/turbostar/projects/<hash>/config.ini)
	std::string cache_root = fs_utils::get_project_cache_root();
	if (!cache_root.empty()) {
		std::string project_config_path = (fs::path(cache_root) / "config.ini").string();
		if (fs::exists(project_config_path)) {
			config_manager::get_instance().load_from_file(project_config_path);
		}
	}

	// Load local project config file (.turbostar_project or .turbostar) if available in project_root_
	if (!project_root_.empty()) {
		std::string local_cfg = (fs::path(project_root_) / ".turbostar_project").string();
		if (fs::exists(local_cfg)) {
			config_manager::get_instance().load_from_file(local_cfg);
		} else {
			local_cfg = (fs::path(project_root_) / ".turbostar").string();
			if (fs::exists(local_cfg)) {
				config_manager::get_instance().load_from_file(local_cfg);
			}
		}
	}

	// Auto-detect build system if not explicitly configured by project settings
	config_manager::get_instance().auto_detect_build_system(project_root_);

	// Clean up previous runs' crash dumps on startup
	crashdump_manager::get_instance().clear_all();

	// Load code reviews for the active project
	codereview_manager::get_instance().load_project(project_root_);

	lsp_manager_ = std::make_unique<lsp_manager>();

	load_instructions();
	scan_dependencies();

	// Start the inventory thread with a 100ms delay
	inventory_thread_ = std::jthread([this](std::stop_token stop) {
		event_logger::get_instance().log("Thread started: project_manager inventory_thread");
		if (!interruptible_sleep(stop, std::chrono::milliseconds(100))) {
			inventory_project(stop);
		}
		event_logger::get_instance().log("Thread exited: project_manager inventory_thread");
	});
}

static bool is_header(const fs::path &path)
{
	std::string ext = path.extension().string();
	return ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx";
}

static bool is_source(const fs::path &path)
{
	std::string ext = path.extension().string();
	return ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".py" || ext == ".go" || ext == ".rs" ||
	       ext == ".js" || ext == ".ts" || ext == ".java" || ext == ".sh";
}
static bool is_doc_config(const fs::path &path)
{
	std::string ext = path.extension().string();
	if (ext == ".md" || ext == ".txt" || ext == ".json" || ext == ".yaml" || ext == ".yml" || ext == ".toml")
		return true;

	std::string filename = path.filename().string();
	return filename == "meson.build" || filename == "CMakeLists.txt" || filename == "Makefile" || filename == "Dockerfile" ||
	       filename == "GEMINI.md" || filename == "AGENTS.md";
}

void project_manager::inventory_project(std::stop_token stop)
{
	auto start_time = std::chrono::steady_clock::now();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	fs::path root(project_root_);

	std::map<std::string, directory_info> dir_map;
	std::vector<std::string> key_files;
	int dir_count = 0;

	// Initial set of potential key files in root
	auto is_root_key_file = [](const std::string &filename) {
		return filename == "meson.build" || filename == "CMakeLists.txt" || filename == "configure.ac" || filename == "Makefile" ||
		       filename == "README.md" || filename == "TODO.md";
	};

	try {
		for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
		     it != fs::recursive_directory_iterator(); ++it) {
			if (stop.stop_requested())
				return;

			const auto &path = it->path();
			std::string rel_path = fs::relative(path, root).string();

			// Skip hidden directories (like .git), build directories, and temp directories
			if (it->is_directory()) {
				std::string name = path.filename().string();
				bool is_top_level = !path.parent_path().has_relative_path() || path.parent_path() == root;

				if (name.front() == '.' || name == build_dir || name == "tmp" || name == "temp" ||
				    (is_top_level && name.starts_with("build"))) {
					it.disable_recursion_pending();
					continue;
				}

				dir_count++;
				if (dir_count > 50) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}

				dir_map[rel_path].path = rel_path;
				dir_map[rel_path].depth = std::count(rel_path.begin(), rel_path.end(), fs::path::preferred_separator);
			} else if (fs::is_regular_file(path)) {
				std::string filename = path.filename().string();
				if (filename.empty() || filename.back() == '~')
					continue;

				// Check for root-level key files
				if (path.parent_path() == root && is_root_key_file(filename)) {
					key_files.push_back(filename);
				}

				bool header = is_header(path);
				bool source = is_source(path);
				bool doc = is_doc_config(path);

				if (!header && !source && !doc)
					continue;

				// Update parent directory
				std::string parent_rel = fs::relative(path.parent_path(), root).string();
				if (parent_rel != ".") {
					auto &info = dir_map[parent_rel];
					info.path = parent_rel;
					if (header)
						info.direct_headers++;
					else if (source)
						info.direct_files++;
					else if (doc)
						info.direct_docs_config++;
				}

				// Update all ancestors for total count
				fs::path p = path.parent_path();
				while (p != root && p.has_relative_path()) {
					std::string ancestor_rel = fs::relative(p, root).string();
					dir_map[ancestor_rel].total_files_underneath++;
					p = p.parent_path();
				}
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Error during project inventory: {}", e.what());
	}

	// Finalize Top 15/18
	std::vector<directory_info> all_dirs;
	for (auto &pair : dir_map) {
		all_dirs.push_back(pair.second);
	}

	auto tie_breaker = [](const directory_info &a, const directory_info &b) {
		if (a.depth != b.depth)
			return a.depth < b.depth;
		return a.path < b.path;
	};

	auto top_recursive = all_dirs;
	std::sort(top_recursive.begin(), top_recursive.end(), [&](const directory_info &a, const directory_info &b) {
		if (a.total_files_underneath != b.total_files_underneath)
			return a.total_files_underneath > b.total_files_underneath;
		return tie_breaker(a, b);
	});

	auto top_headers = all_dirs;
	std::sort(top_headers.begin(), top_headers.end(), [&](const directory_info &a, const directory_info &b) {
		if (a.direct_headers != b.direct_headers)
			return a.direct_headers > b.direct_headers;
		return tie_breaker(a, b);
	});

	auto top_source = all_dirs;
	std::sort(top_source.begin(), top_source.end(), [&](const directory_info &a, const directory_info &b) {
		if (a.direct_files != b.direct_files)
			return a.direct_files > b.direct_files;
		return tie_breaker(a, b);
	});

	auto top_docs = all_dirs;
	std::sort(top_docs.begin(), top_docs.end(), [&](const directory_info &a, const directory_info &b) {
		if (a.direct_docs_config != b.direct_docs_config)
			return a.direct_docs_config > b.direct_docs_config;
		return tie_breaker(a, b);
	});

	std::set<std::string> selected_paths;
	std::vector<directory_info> result;

	auto add_top = [&](const std::vector<directory_info> &list, size_t count, int min_val = 0) {
		size_t added = 0;
		for (const auto &info : list) {
			if (added >= count)
				break;
			if (selected_paths.contains(info.path))
				continue;

			int val = 0;
			if (&list == &top_recursive)
				val = info.total_files_underneath;
			else if (&list == &top_headers)
				val = info.direct_headers;
			else if (&list == &top_source)
				val = info.direct_files;
			else if (&list == &top_docs)
				val = info.direct_docs_config;

			if (val <= min_val)
				continue;

			result.push_back(info);
			selected_paths.insert(info.path);
			added++;
		}
	};

	add_top(top_recursive, 5);
	add_top(top_headers, 5);
	add_top(top_source, 5);
	add_top(top_docs, 3, 1);

	// Try to find 'main' using common paths
	static const std::vector<std::string> common_mains = {"src/main.cpp", "src/main.c", "main.cpp", "main.c"};
	for (const auto &m : common_mains) {
		if (fs::exists(root / m)) {
			key_files.push_back(m);
			break;
		}
	}

	// Final sort by path for display
	std::sort(result.begin(), result.end(), [](const directory_info &a, const directory_info &b) { return a.path < b.path; });

	std::sort(key_files.begin(), key_files.end());
	key_files.erase(std::unique(key_files.begin(), key_files.end()), key_files.end());

	{
		std::lock_guard<std::mutex> lock(layout_mutex_);
		layout_.top_directories = std::move(result);
		layout_.key_files = std::move(key_files);
		layout_.ready = true;
	}

	auto end_time = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
	event_logger::get_instance().log("Project inventory complete in {}ms. Indexed {} directories.", duration.count(), dir_count);
}

std::string project_manager::get_project_layout_markdown() const
{
	std::lock_guard<std::mutex> lock(layout_mutex_);
	if (!layout_.ready || (layout_.top_directories.empty() && layout_.key_files.empty()))
		return "";

	std::stringstream ss;
	if (!layout_.top_directories.empty()) {
		ss << "\nProject Layout Overview:\n";
		ss << "| Directory | Files | Headers | Docs/Config | Total Underneath |\n";
		ss << "| :--- | :---: | :---: | :---: | :---: |\n";

		for (const auto &dir : layout_.top_directories) {
			ss << "| " << dir.path << " | " << dir.direct_files << " | " << dir.direct_headers << " | "
			   << dir.direct_docs_config << " | " << dir.total_files_underneath << " |\n";
		}
	}

	if (!layout_.key_files.empty()) {
		ss << "\nKey Files:\n";
		for (const auto &f : layout_.key_files) {
			ss << "- " << f << "\n";
		}
	}
	return ss.str();
}

std::string project_manager::get_project_knowledge_prompt() const
{
	std::string prompt;

	std::string project_instr = get_project_instructions();
	if (!project_instr.empty()) {
		prompt += "\n\nProject-specific instructions and engineering standards:\n" + project_instr;
	}

	std::string clang_format = get_clang_format();
	if (!clang_format.empty()) {
		prompt += "\n\nProject formatting rules (.clang-format):\n```yaml\n" + clang_format + "```\n";
	}

	std::string project_layout = get_project_layout_markdown();
	if (!project_layout.empty()) {
		prompt += "\n" + project_layout;
	}

	std::vector<std::pair<std::string, std::string>> mapped_deps = get_mapped_dependencies();
	if (!mapped_deps.empty()) {
		prompt += "\n\nRecognized Project Dependencies (VFS Paths):\n"
			  "The current project has dependencies configured that are available via the Virtual Filesystem (VFS).\n"
			  "You can use the `fs_read_lines`, `fs_list_dir`, and other `fs_*` tool calls to inspect the source code, "
			  "headers, and any documentation for these dependencies using the following VFS paths:\n\n"
			  "| dependency | VFS path |\n"
			  "| --- | --- |\n";
		for (const auto &pair : mapped_deps) {
			prompt += std::format("| {} | {} |\n", pair.first, pair.second);
		}
	}

	std::string repo_url = git_manager::get_instance().get_remote_origin_url();
	std::string repo_branch = git_manager::get_instance().get_current_branch();
	if (!repo_url.empty()) {
		prompt += "\n\nProject Repository & Git Information:\n";
		prompt += std::format("- Upstream Git Repository URL: {}\n", repo_url);
		if (!repo_branch.empty()) {
			prompt += std::format("- Active Git Branch: {}\n", repo_branch);
		}
		prompt += "When delegating tasks to remote A2A agents or referencing the project repository, you can provide this repository URL and branch.\n";
	}

	return prompt;
}

void project_manager::load_instructions()
{
	if (project_root_.empty())
		return;

	fs::path root(project_root_);
	fs::path agent_md = root / "AGENTS.md";
	fs::path gemini_md = root / "GEMINI.md";

	fs::path target;
	if (fs::exists(agent_md)) {
		target = agent_md;
	} else if (fs::exists(gemini_md)) {
		target = gemini_md;
	}

	if (!target.empty()) {
		std::ifstream file(target);
		if (file.is_open()) {
			std::stringstream ss;
			ss << file.rdbuf();
			instructions_ = ss.str();
			event_logger::get_instance().log("Loaded project instructions from {}", target.string());
		}
	}

	fs::path clang_format_path = root / ".clang-format";
	if (fs::exists(clang_format_path)) {
		std::ifstream file(clang_format_path);
		if (file.is_open()) {
			std::string line;
			std::string minified;
			int line_count = 0;
			while (std::getline(file, line)) {
				// Trim leading whitespace
				line.erase(0, line.find_first_not_of(" \t\r\n"));
				// Skip empty lines and comments
				if (!line.empty() && line[0] != '#') {
					minified += line + "\n";
					line_count++;
				}
				if (line_count > 100) {
					event_logger::get_instance().log(
					    ".clang-format too large (>100 active lines), ignoring for LLM prompt.");
					minified.clear();
					break;
				}
			}
			if (!minified.empty()) {
				clang_format_ = minified;
				event_logger::get_instance().log("Loaded and minified .clang-format ({} lines)", line_count);
			}
		}
	}
}

void project_manager::lsp_start(event_queue &queue)
{
	if (lsp_manager_)
		lsp_manager_->start(queue);
}
void project_manager::lsp_stop()
{
	if (lsp_manager_)
		lsp_manager_->stop();
}
void project_manager::lsp_open_document(const std::string &filepath, const std::string &text)
{
	if (lsp_manager_)
		lsp_manager_->open_document(filepath, text);
}
void project_manager::lsp_update_document(const std::string &filepath, const std::string &text)
{
	if (lsp_manager_)
		lsp_manager_->update_document(filepath, text);
}
void project_manager::lsp_request_hover(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		lsp_manager_->request_hover(filepath, line, character);
}
void project_manager::lsp_request_document_highlight(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		lsp_manager_->request_document_highlight(filepath, line, character);
}
void project_manager::lsp_request_selection_range(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		lsp_manager_->request_selection_range(filepath, line, character);
}
bool project_manager::lsp_is_supported_file(const std::string &filepath) const
{
	if (lsp_manager_)
		return lsp_manager_->is_supported_file(filepath);
	return false;
}

std::vector<text_range> project_manager::lsp_query_selection_ranges(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_selection_ranges(filepath, line, character);
	return {};
}

std::vector<lsp_manager::location_info> project_manager::lsp_query_definition(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_definition(filepath, line, character);
	return {};
}

std::vector<lsp_manager::location_info> project_manager::lsp_query_references(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_references(filepath, line, character);
	return {};
}

std::vector<lsp_manager::symbol_info> project_manager::lsp_query_workspace_symbols(const std::string &query)
{
	if (lsp_manager_)
		return lsp_manager_->query_workspace_symbols(query);
	return {};
}

std::vector<lsp_manager::symbol_node> project_manager::lsp_query_document_symbols(const std::string &filepath)
{
	if (lsp_manager_)
		return lsp_manager_->query_document_symbols(filepath);
	return {};
}

void project_manager::lsp_invalidate_symbol_cache(const std::string &filepath)
{
	if (lsp_manager_)
		lsp_manager_->invalidate_symbol_cache(filepath);
}

std::vector<lsp_manager::call_hierarchy_item> project_manager::lsp_query_call_hierarchy_outgoing(const std::string &filepath, int line,
												 int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_call_hierarchy_outgoing(filepath, line, character);
	return {};
}

std::vector<lsp_manager::outgoing_call_item> project_manager::lsp_query_call_hierarchy_outgoing_batch(
	const std::string &filepath,
	const std::vector<std::pair<int, int>> &positions,
	std::chrono::steady_clock::time_point deadline)
{
	if (lsp_manager_)
		return lsp_manager_->query_call_hierarchy_outgoing_batch(filepath, positions, deadline);
	return {};
}

std::vector<lsp_manager::type_hierarchy_item> project_manager::lsp_query_type_hierarchy_supertypes(const std::string &filepath, int line,
												   int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_type_hierarchy_supertypes(filepath, line, character);
	return {};
}

std::optional<std::vector<diagnostic_info>> project_manager::lsp_query_file_diagnostics(const std::string &filepath)
{
	if (lsp_manager_)
		return lsp_manager_->query_file_diagnostics(filepath);
	return std::nullopt;
}

std::vector<std::string> project_manager::get_available_tests()
{
	std::lock_guard<std::mutex> lock(available_tests_mutex_);
	if (!tests_ready_) {
		refresh_available_tests();
	} else if (build_definition_changed()) {
		// The build definition (e.g. meson.build) has been modified since we last
		// listed tests, so the cached list may not include newly-registered test
		// binaries. Refresh once so additions are discoverable without a restart.
		event_logger::get_instance().log("project_manager: build definition changed, refreshing available tests.");
		refresh_available_tests();
	} else if (tests_list_refreshed_at_ != std::chrono::steady_clock::time_point{}
		   && std::chrono::steady_clock::now() - tests_list_refreshed_at_ > std::chrono::minutes(5)) {
		// Upper bound on staleness: regardless of meson.build mtime, refresh a
		// list that hasn't been rebuilt in >5 minutes so tests added through
		// other means eventually appear, while keeping caching effective for
		// back-to-back lookups.
		event_logger::get_instance().log("project_manager: available tests cache is >5 minutes old, refreshing.");
		refresh_available_tests();
	}
	return available_tests_;
}

void project_manager::invalidate_available_tests_cache() noexcept
{
	std::lock_guard<std::mutex> lock(available_tests_mutex_);
	tests_ready_ = false;
}

static bool is_build_directory_valid(const fs::path &dir, std::string_view build_system)
{
	std::error_code ec;
	if (!fs::is_directory(dir, ec)) {
		return false;
	}
	if (build_system == "meson") {
		return fs::exists(dir / "build.ninja", ec);
	}
	if (build_system == "cmake") {
		return fs::exists(dir / "CMakeCache.txt", ec) ||
		       fs::exists(dir / "CTestTestfile.cmake", ec) ||
		       fs::exists(dir / "build.ninja", ec) ||
		       fs::exists(dir / "Makefile", ec);
	}
	// General/fallback check
	return fs::exists(dir / "build.ninja", ec) ||
	       fs::exists(dir / "CMakeCache.txt", ec) ||
	       fs::exists(dir / "CTestTestfile.cmake", ec) ||
	       fs::exists(dir / "Makefile", ec);
}

std::vector<std::string> project_manager::parse_ctest_test_list(std::string_view output)
{
	std::vector<std::string> tests;
	std::istringstream ss{std::string(output)};
	std::string line;
	while (std::getline(ss, line)) {
		if (line.empty()) {
			continue;
		}
		// Trim whitespace
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);
		if (line.empty()) {
			continue;
		}
		// Lines from `ctest -N` look like:
		// "Test  #1: assert-test"
		// "Test #10: ostream-test"
		if (!line.starts_with("Test")) {
			continue;
		}
		size_t hash_pos = line.find('#');
		if (hash_pos == std::string::npos) {
			continue;
		}
		std::string_view prefix = std::string_view(line).substr(0, hash_pos);
		while (!prefix.empty() && (prefix.back() == ' ' || prefix.back() == '\t')) {
			prefix.remove_suffix(1);
		}
		if (prefix != "Test") {
			continue;
		}

		size_t colon_pos = line.find(':', hash_pos + 1);
		if (colon_pos == std::string::npos) {
			continue;
		}

		bool has_digit = false;
		bool valid_num = true;
		for (size_t i = hash_pos + 1; i < colon_pos; ++i) {
			if (std::isdigit(static_cast<unsigned char>(line[i]))) {
				has_digit = true;
			} else if (line[i] == ' ' || line[i] == '\t') {
				// skip whitespace
			} else {
				valid_num = false;
				break;
			}
		}
		if (!has_digit || !valid_num) {
			continue;
		}

		std::string_view name_view = std::string_view(line).substr(colon_pos + 1);
		while (!name_view.empty() && (name_view.front() == ' ' || name_view.front() == '\t')) {
			name_view.remove_prefix(1);
		}
		while (!name_view.empty() && (name_view.back() == ' ' || name_view.back() == '\t')) {
			name_view.remove_suffix(1);
		}
		if (!name_view.empty()) {
			tests.emplace_back(name_view);
		}
	}
	return tests;
}

bool project_manager::build_definition_changed() const
{
	std::string build_system = config_manager::get_instance().get_build_system();
	if (build_system != "meson" && build_system != "cmake") {
		return false;
	}
	if (tests_build_def_mtime_ == std::filesystem::file_time_type{}) {
		return false;
	}
	fs::path build_def;
	if (build_system == "meson") {
		build_def = fs::path(project_root_) / "meson.build";
	} else if (build_system == "cmake") {
		build_def = fs::path(project_root_) / "CMakeLists.txt";
	}
	std::error_code ec;
	auto mtime = fs::last_write_time(build_def, ec);
	if (ec) {
		return false;
	}
	return mtime != tests_build_def_mtime_;
}

std::string project_manager::resolve_build_dir() const
{
	std::string build_system = config_manager::get_instance().get_build_system();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	fs::path build_path(build_dir);
	if (build_path.is_relative() && !project_root_.empty()) {
		build_path = fs::path(project_root_) / build_path;
	}

	std::error_code ec;
	if (!build_path.empty() && is_build_directory_valid(build_path, build_system)) {
		return build_path.string();
	}

	// Fallback: scan the project root for a directory containing build artifacts
	// (e.g. build/) so test listing works even when no build dir is configured
	// (headless / unit-test environments).
	if (!project_root_.empty()) {
		ec.clear();
		for (const auto &entry : fs::directory_iterator(project_root_, fs::directory_options::skip_permission_denied, ec)) {
			if (ec) {
				break;
			}
			std::error_code ec2;
			if (entry.is_directory(ec2) && is_build_directory_valid(entry.path(), build_system)) {
				return entry.path().string();
			}
		}
	}
	return (fs::is_directory(build_path, ec) && is_build_directory_valid(build_path, build_system)) ? build_path.string() : "";
}

void project_manager::refresh_available_tests()
{
	std::string build_system = config_manager::get_instance().get_build_system();
	fs::path build_path(resolve_build_dir());

	std::string cmd;

	if (build_system == "meson") {
		cmd = std::format("meson test -C {} --list", build_path.string());
	} else if (build_system == "cmake") {
		cmd = std::format("ctest --test-dir {} -N", build_path.string());
	} else {
		// Fallback or not supported for other systems yet
		tests_ready_ = true;
		available_tests_.clear();
		tests_list_refreshed_at_ = std::chrono::steady_clock::now();
		return;
	}

	// Record the current build definition file mtime so a subsequent change is detected by
	// build_definition_changed(). Capture before the (potentially slow) listing so
	// an edit racing with this refresh still triggers the next refresh.
	std::error_code ec;
	fs::path build_def;
	if (build_system == "meson") {
		build_def = fs::path(project_root_) / "meson.build";
	} else if (build_system == "cmake") {
		build_def = fs::path(project_root_) / "CMakeLists.txt";
	}

	if (!build_def.empty()) {
		auto mtime = fs::last_write_time(build_def, ec);
		if (!ec) {
			tests_build_def_mtime_ = mtime;
		} else {
			event_logger::get_instance().log("project_manager: could not stat {}: {}", build_def.string(), ec.message());
		}
	}

	sync_command_runner runner;
	runner.apply_internal_profile();
	runner.set_project_dir(project_root_);

	std::string output = runner.execute_and_get_output(cmd);
	// Record the refresh time regardless of success/failure so a failing command
	// does not cause every subsequent lookup to retry the slow command in a tight loop.
	tests_list_refreshed_at_ = std::chrono::steady_clock::now();
	if (runner.get_exit_code() != 0) {
		event_logger::get_instance().log("Failed to list tests: {}", output);
		tests_ready_ = true;
		available_tests_.clear();
		return;
	}

	available_tests_.clear();
	if (build_system == "meson") {
		std::stringstream ss(output);
		std::string line;
		while (std::getline(ss, line)) {
			if (line.empty())
				continue;
			// Trim potential whitespace
			line.erase(0, line.find_first_not_of(" \t\r\n"));
			line.erase(line.find_last_not_of(" \t\r\n") + 1);
			if (line.empty())
				continue;
			// `meson test --list` prefixes each test with a display-group label (e.g.
			// "agent - turbostar:unit_..."). That prefix is not part of the runnable Meson
			// test selector, so strip everything up to and including the first " - ". This keeps
			// the returned names directly usable with `meson test <name>` (and by fs_run_tests).
			auto sep = line.find(" - ");
			if (sep != std::string::npos) {
				line = line.substr(sep + 3);
			}
			if (!line.empty()) {
				available_tests_.push_back(line);
			}
		}
	} else if (build_system == "cmake") {
		available_tests_ = parse_ctest_test_list(output);
	}

	std::sort(available_tests_.begin(), available_tests_.end());
	tests_ready_ = true;
	event_logger::get_instance().log("Refreshed available tests: {} tests found.", available_tests_.size());
}


void project_manager::shutdown()
{
	is_exiting_ = true;
	inventory_thread_.request_stop();
	if (lsp_manager_) {
		lsp_manager_->stop();
	}
}

std::vector<std::string> project_manager::detect_executable_candidates()
{
	std::vector<std::string> candidates;
	std::string proj_root = project_root_;

	std::filesystem::path meson_path = std::filesystem::path(proj_root) / "meson.build";
	if (!std::filesystem::exists(meson_path)) {
		return candidates;
	}

	std::ifstream file(meson_path);
	if (!file.is_open()) {
		return candidates;
	}

	std::string line;
	std::string project_name;
	std::regex project_regex(R"(project\s*\(\s*['"]([^'"]+)['"])", std::regex_constants::ECMAScript);
	std::regex exe_regex(R"(executable\s*\(\s*['"]([^'"]+)['"])", std::regex_constants::ECMAScript);
	std::smatch match;

	while (std::getline(file, line)) {
		// Strip comments first
		std::string trimmed = line;
		trimmed.erase(0, trimmed.find_first_not_of(" \t"));
		if (trimmed.empty() || trimmed[0] == '#') {
			continue;
		}

		if (project_name.empty()) {
			if (std::regex_search(line, match, project_regex)) {
				if (match.size() > 1) {
					project_name = match[1].str();
				}
			}
		}

		if (std::regex_search(line, match, exe_regex)) {
			if (match.size() > 1) {
				std::string exe_name = match[1].str();
				if (std::find(candidates.begin(), candidates.end(), exe_name) == candidates.end()) {
					candidates.push_back(exe_name);
				}
			}
		}
	}

	// Prioritize candidate matching project name at the top
	if (!project_name.empty()) {
		auto it = std::find(candidates.begin(), candidates.end(), project_name);
		if (it != candidates.end()) {
			std::string p_name = *it;
			candidates.erase(it);
			candidates.insert(candidates.begin(), p_name);
		}
	}

	return candidates;
}

std::vector<std::string> project_manager::get_detected_dependencies() const
{
	std::lock_guard<std::mutex> lock(dependencies_mutex_);
	return detected_dependencies_;
}

std::vector<std::string> project_manager::get_github_vfs_urls() const
{
	std::lock_guard<std::mutex> lock(dependencies_mutex_);
	return github_vfs_urls_;
}

std::vector<std::pair<std::string, std::string>> project_manager::get_mapped_dependencies() const
{
	std::lock_guard<std::mutex> lock(dependencies_mutex_);
	return mapped_dependencies_;
}

void project_manager::scan_dependencies()
{
	std::vector<std::string> deps;
	std::vector<std::string> urls;
	std::vector<std::pair<std::string, std::string>> mapped_deps;

	std::string proj_root = project_root_;
	std::filesystem::path meson_path = std::filesystem::path(proj_root) / "meson.build";
	if (std::filesystem::exists(meson_path)) {
		std::ifstream file(meson_path);
		if (file.is_open()) {
			std::string line;
			std::regex dep_regex(R"(dependency\s*\(\s*['"]([^'"]+)['"])",
					     std::regex_constants::ECMAScript | std::regex_constants::icase);
			std::smatch match;

			while (std::getline(file, line)) {
				// Strip comments
				std::string trimmed = line;
				trimmed.erase(0, trimmed.find_first_not_of(" \t"));
				if (trimmed.empty() || trimmed[0] == '#') {
					continue;
				}

				if (std::regex_search(line, match, dep_regex)) {
					if (match.size() > 1) {
						std::string dep_name = match[1].str();
						if (std::find(deps.begin(), deps.end(), dep_name) == deps.end()) {
							deps.push_back(dep_name);
						}
					}
				}
			}
		}
	}

	// Lookup known dependencies to get github:// URLs
	static const std::unordered_map<std::string, std::string> known_deps = {
		{"ncursesw",      "github://mirror/ncurses"},
		{"ncurses",       "github://mirror/ncurses"},
		{"cpp-httplib",   "github://yhirose/cpp-httplib"},
		{"re2",           "github://google/re2"},
		{"sqlite3",       "github://sqlite/sqlite"},
		{"nlohmann_json", "github://nlohmann/json"},
		{"zydis",         "github://zyantific/zydis"},
		{"sdl2",          "github://libsdl-org/SDL@SDL2/"}
	};

	for (const auto &dep_name : deps) {
		std::string dep_lower = dep_name;
		std::transform(dep_lower.begin(), dep_lower.end(), dep_lower.begin(),
			       [](unsigned char c) { return std::tolower(c); });

		auto it = known_deps.find(dep_lower);
		if (it != known_deps.end()) {
			if (std::find(urls.begin(), urls.end(), it->second) == urls.end()) {
				urls.push_back(it->second);
			}
			mapped_deps.push_back({dep_name, it->second});
		}
	}

	{
		std::lock_guard<std::mutex> lock(dependencies_mutex_);
		detected_dependencies_ = std::move(deps);
		github_vfs_urls_ = std::move(urls);
		mapped_dependencies_ = std::move(mapped_deps);
	}
}
