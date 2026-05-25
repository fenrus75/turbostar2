#include "project_manager.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <nlohmann/json.hpp>
#include <re2/re2.h>
#include "command_runner.h"
#include "config_manager.h"
#include "crashdump_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"

namespace fs = std::filesystem;

// JSON serialization macros for software map types
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(text_range, start_y, start_x, end_y, end_x);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(lsp_manager::location_info, path, range);
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(project_manager::software_map_symbol, name, kind, location, looked_up_count, accumulated_count,
				   is_seed, is_sampled, base_classes);

project_manager &project_manager::get_instance()
{
	static project_manager instance;
	return instance;
}

void project_manager::initialize()
{
	repo_root_ = git_manager::get_instance().get_repository_root();
	if (repo_root_.empty()) {
		repo_root_ = fs::current_path().string();
	}

	// Clean up previous runs' crash dumps on startup
	crashdump_manager::get_instance().clear_all();

	lsp_manager_ = std::make_unique<lsp_manager>();

	load_instructions();

	// Start the inventory thread with a 100ms delay
	inventory_thread_ = std::jthread([this](std::stop_token stop) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (stop.stop_requested())
			return;
		inventory_project(stop);
	});

	// Start the software map thread with a 2000ms delay to let the LSP warm up
	software_map_thread_ = std::jthread([this](std::stop_token stop) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		if (stop.stop_requested())
			return;
		software_map_loop(stop);
	});
}

static bool is_header(const fs::path &path)
{
	static const std::set<std::string> header_exts = {".h", ".hpp", ".hh", ".hxx"};
	return header_exts.contains(path.extension().string());
}

static bool is_source(const fs::path &path)
{
	static const std::set<std::string> source_exts = {".c", ".cpp", ".cc", ".cxx", ".py", ".go", ".rs", ".js", ".ts",
							  ".java", ".sh"};
	return source_exts.contains(path.extension().string());
}

static bool is_doc_config(const fs::path &path)
{
	static const std::set<std::string> doc_exts = {".md", ".txt", ".json", ".yaml", ".yml", ".toml"};
	static const std::set<std::string> doc_files = {"meson.build", "CMakeLists.txt", "Makefile", "Dockerfile", "GEMINI.md",
							"AGENTS.md"};

	if (doc_exts.contains(path.extension().string()))
		return true;
	if (doc_files.contains(path.filename().string()))
		return true;
	return false;
}

void project_manager::inventory_project(std::stop_token stop)
{
	auto start_time = std::chrono::steady_clock::now();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	fs::path root(repo_root_);

	std::map<std::string, directory_info> dir_map;
	std::vector<std::string> key_files;
	int dir_count = 0;

	// Initial set of potential key files in root
	static const std::set<std::string> root_key_filenames = {"meson.build", "CMakeLists.txt", "configure.ac", "Makefile",
								 "README.md",   "TODO.md"};

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
				if (path.parent_path() == root && root_key_filenames.contains(filename)) {
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
		event_logger::get_instance().log("Error during project inventory: " + std::string(e.what()));
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
	std::sort(result.begin(), result.end(), [](const directory_info &a, const directory_info &b) {
		return a.path < b.path;
	});

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
	event_logger::get_instance().log("Project inventory complete in " + std::to_string(duration.count()) +
					 "ms. Indexed " + std::to_string(dir_count) + " directories.");
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

	if (config_manager::get_instance().is_software_map_enabled()) {
		std::string software_map = get_software_map_markdown();
		if (!software_map.empty()) {
			prompt += "\n" + software_map;
		}
	}

	return prompt;
}

void project_manager::load_instructions()
{
	if (repo_root_.empty())
		return;

	fs::path root(repo_root_);
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
			event_logger::get_instance().log("Loaded project instructions from " + target.string());
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
				event_logger::get_instance().log("Loaded and minified .clang-format (" + std::to_string(line_count) +
								 " lines)");
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

std::vector<lsp_manager::call_hierarchy_item> project_manager::lsp_query_call_hierarchy_outgoing(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_call_hierarchy_outgoing(filepath, line, character);
	return {};
}

std::vector<lsp_manager::type_hierarchy_item> project_manager::lsp_query_type_hierarchy_supertypes(const std::string &filepath, int line, int character)
{
	if (lsp_manager_)
		return lsp_manager_->query_type_hierarchy_supertypes(filepath, line, character);
	return {};
}

std::vector<std::string> project_manager::get_available_tests()
{
	if (!tests_ready_) {
		refresh_available_tests();
	}
	return available_tests_;
}

void project_manager::refresh_available_tests()
{
	std::string build_system = config_manager::get_instance().get_build_system();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	
	fs::path build_path(build_dir);
	if (build_path.is_relative()) {
		build_path = fs::path(repo_root_) / build_path;
	}

	std::string cmd;

	if (build_system == "meson") {
		cmd = "meson test -C " + build_path.string() + " --list";
	} else {
		// Fallback or not supported for other systems yet
		tests_ready_ = true;
		available_tests_.clear();
		return;
	}

	sync_command_runner runner;
	runner.apply_internal_profile();
	runner.set_project_dir(repo_root_);

	std::string output = runner.execute_and_get_output(cmd);
	if (runner.get_exit_code() != 0) {
		event_logger::get_instance().log("Failed to list tests: " + output);
		tests_ready_ = true;
		available_tests_.clear();
		return;
	}

	available_tests_.clear();
	std::stringstream ss(output);
	std::string line;
	while (std::getline(ss, line)) {
		if (line.empty())
			continue;
		// Trim potential whitespace
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);
		if (!line.empty()) {
			available_tests_.push_back(line);
		}
	}

	std::sort(available_tests_.begin(), available_tests_.end());
	tests_ready_ = true;
	event_logger::get_instance().log("Refreshed available tests: " + std::to_string(available_tests_.size()) + " tests found.");
}

std::string project_manager::get_software_map_markdown() const
{
	std::shared_lock<std::shared_mutex> lock(software_map_markdown_mutex_);
	if (software_map_markdown_cache_.empty()) {
		return "Software Map is currently building or empty.";
	}
	return software_map_markdown_cache_;
}

void project_manager::update_software_map_markdown()
{
	std::vector<software_map_symbol> classes;
	std::vector<software_map_symbol> functions;

	{
		std::shared_lock<std::shared_mutex> lock(software_map_mutex_);
		
		bool has_non_zero = false;
		for (const auto &sym : software_map_.symbols) {
			if (sym.accumulated_count > 0) {
				has_non_zero = true;
				break;
			}
		}

		for (const auto &sym : software_map_.symbols) {
			if (has_non_zero && sym.accumulated_count == 0)
				continue;

			if (sym.kind == 5 || sym.kind == 22 || sym.kind == 11) {
				classes.push_back(sym);
			} else if (sym.kind == 12 || sym.kind == 6) {
				functions.push_back(sym);
			}
		}
	}

	auto sort_desc = [](const software_map_symbol &a, const software_map_symbol &b) {
		return a.accumulated_count > b.accumulated_count;
	};

	std::sort(classes.begin(), classes.end(), sort_desc);
	std::sort(functions.begin(), functions.end(), sort_desc);

	std::string md = "## Automatic Software Map\n\n";

	int total_limit = 50;
	int class_limit = 25;
	int classes_shown = 0;

	// Classes Table
	if (!classes.empty()) {
		md += "### Key Classes & Structs\n";
		md += "| Class Name | Type | Base Class(es) | File Location |\n";
		md += "| :--- | :--- | :--- | :--- |\n";
		for (const auto &sym : classes) {
			if (classes_shown >= class_limit)
				break;
			std::string kind_str = (sym.kind == 5) ? "Class" : (sym.kind == 22 ? "Struct" : "Interface");

			std::string rel_path = sym.location.path;
			if (!repo_root_.empty() && rel_path.starts_with(repo_root_)) {
				rel_path = rel_path.substr(repo_root_.length());
				if (!rel_path.empty() && rel_path.front() == '/')
					rel_path.erase(0, 1);
			}

			std::string bases = sym.base_classes.empty() ? "-" : sym.base_classes;
			md += "| `" + sym.name + "` | " + kind_str + " | " + bases + " | `" + rel_path + "` |\n";
			classes_shown++;
		}
		md += "\n";
	}

	// Functions Table
	if (!functions.empty()) {
		md += "### Key Functions & Methods\n";
		md += "| Function Name | File Location |\n";
		md += "| :--- | :--- |\n";
		int func_limit = total_limit - classes_shown;
		int funcs_shown = 0;
		for (const auto &sym : functions) {
			if (funcs_shown >= func_limit)
				break;

			std::string rel_path = sym.location.path;
			if (!repo_root_.empty() && rel_path.starts_with(repo_root_)) {
				rel_path = rel_path.substr(repo_root_.length());
				if (!rel_path.empty() && rel_path.front() == '/')
					rel_path.erase(0, 1);
			}

			md += "| `" + sym.name + "` | `" + rel_path + "` |\n";
			funcs_shown++;
		}
	}

	{
		std::unique_lock<std::shared_mutex> lock(software_map_markdown_mutex_);
		software_map_markdown_cache_ = std::move(md);
	}
}

void project_manager::save_software_map()
{
	std::string cache_root = fs_utils::get_project_cache_root();
	if (cache_root.empty())
		return;

	fs::path cache_path = fs::path(cache_root) / "software_map.json";

	nlohmann::json j;
	{
		std::shared_lock<std::shared_mutex> lock(software_map_mutex_);
		j["symbols"] = software_map_.symbols;
		j["git_head_hash"] = software_map_.git_head_hash;
		j["ready"] = software_map_.ready;
	}

	{
		std::shared_lock<std::shared_mutex> lock(software_map_markdown_mutex_);
		j["markdown_cache"] = software_map_markdown_cache_;
	}

	std::ofstream file(cache_path);
	if (file.is_open()) {
		file << j.dump(4);
	}
}

void project_manager::load_software_map()
{
	std::string cache_root = fs_utils::get_project_cache_root();
	if (cache_root.empty())
		return;

	fs::path cache_path = fs::path(cache_root) / "software_map.json";
	if (!fs::exists(cache_path))
		return;

	try {
		std::ifstream file(cache_path);
		if (!file.is_open())
			return;

		nlohmann::json j;
		file >> j;

		std::unique_lock<std::shared_mutex> lock(software_map_mutex_);
		software_map_.symbols = j.at("symbols").get<std::vector<software_map_symbol>>();
		if (j.contains("git_head_hash"))
			software_map_.git_head_hash = j.at("git_head_hash").get<std::string>();
		if (j.contains("ready"))
			software_map_.ready = j.at("ready").get<bool>();

		// Rebuild the lookup map
		software_map_.name_to_indices.clear();
		for (size_t i = 0; i < software_map_.symbols.size(); ++i) {
			software_map_.name_to_indices[software_map_.symbols[i].name].push_back(i);
		}

		event_logger::get_instance().log("Loaded Software Map from cache (" + std::to_string(software_map_.symbols.size()) + " symbols).");

		{
			std::unique_lock<std::shared_mutex> lock_md(software_map_markdown_mutex_);
			if (j.contains("markdown_cache"))
				software_map_markdown_cache_ = j.at("markdown_cache").get<std::string>();
		}

		if (software_map_markdown_cache_.empty()) {
			update_software_map_markdown();
		}
		} catch (const std::exception &e) {		event_logger::get_instance().log("Failed to load Software Map from cache: " + std::string(e.what()));
	}
}

void project_manager::software_map_loop(std::stop_token stop)
{
	load_software_map();

	int sample_counter = 0;

	while (!stop.stop_requested()) {
		if (!config_manager::get_instance().is_software_map_enabled()) {
			std::this_thread::sleep_for(std::chrono::seconds(5));
			continue;
		}

		bool needs_seed = false;
		{
			std::shared_lock<std::shared_mutex> lock(software_map_mutex_);
			needs_seed = !software_map_.ready || software_map_.symbols.empty();
		}

		if (needs_seed) {
			// Phase 1: Regex Seeding from Headers
			std::set<std::string> seed_names;
			RE2 class_regex("^\\s*(?:class|struct)\\s+([a-zA-Z_][a-zA-Z0-9_]*)");
			fs::path root(repo_root_);
			std::string build_dir = config_manager::get_instance().get_build_directory();

			try {
				for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
				     it != fs::recursive_directory_iterator(); ++it) {
					if (stop.stop_requested())
						return;
					const auto &path = it->path();
					if (it->is_directory()) {
						std::string name = path.filename().string();
						bool is_top_level = !path.parent_path().has_relative_path() || path.parent_path() == root;
						if (name.front() == '.' || name == build_dir || name == "tmp" || name == "temp" ||
						    (is_top_level && name.starts_with("build"))) {
							it.disable_recursion_pending();
						}
						continue;
					}
					if (is_header(path)) {
						std::ifstream file(path);
						std::string line;
						while (std::getline(file, line)) {
							std::string class_name;
							if (RE2::PartialMatch(line, class_regex, &class_name)) {
								seed_names.insert(class_name);
							}
						}
					}
				}
			} catch (...) {
			}

			auto process_symbols = [&](const std::vector<lsp_manager::symbol_info> &symbols) {
				std::string build_dir_str = "/" + build_dir + "/";
				std::unique_lock<std::shared_mutex> lock(software_map_mutex_);
				for (const auto &sym : symbols) {
					if (sym.location.path.starts_with("/usr/"))
						continue;
						
					// Filter out anything in a build directory
					if (sym.location.path.find(build_dir_str) != std::string::npos || sym.location.path.find("/build") != std::string::npos)
						continue;

					if (sym.kind == 5 || sym.kind == 6 || sym.kind == 11 || sym.kind == 12 || sym.kind == 22) {
						// Simple deduplication
						bool exists = false;
						for (const auto &ex : software_map_.symbols) {
							if (ex.name == sym.name && ex.location.path == sym.location.path) {
								exists = true;
								break;
							}
						}
						if (exists)
							continue;

						software_map_symbol sms;
						sms.name = sym.name;
						sms.kind = sym.kind;
						sms.location = sym.location;
						sms.looked_up_count = 0;
						sms.is_seed = true;
						sms.is_sampled = false;

						std::string rel_path = sym.location.path;
						if (!repo_root_.empty() && rel_path.starts_with(repo_root_)) {
							rel_path = rel_path.substr(repo_root_.length());
							if (!rel_path.empty() && rel_path.front() == '/') {
								rel_path.erase(0, 1);
							}
						}

						int num_slashes = std::count(rel_path.begin(), rel_path.end(), '/');
						int depth_bonus = std::max(0, 10 - num_slashes);
						sms.accumulated_count = depth_bonus;

						if (sym.location.path.find("/include/") != std::string::npos || sym.location.path.ends_with(".h")) {
							sms.accumulated_count += 5;
						}

						software_map_.symbols.push_back(sms);
						software_map_.name_to_indices[sym.name].push_back(software_map_.symbols.size() - 1);
					}
				}
			};

			// Phase 2: Targeted LSP resolution for regex seeds
			for (const auto &name : seed_names) {
				if (stop.stop_requested())
					return;
				auto symbols = lsp_query_workspace_symbols(name);
				process_symbols(symbols);
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}

			// Phase 3: Broad LSP scan fallback
			auto broad_symbols = lsp_query_workspace_symbols("");
			process_symbols(broad_symbols);

			{
				std::unique_lock<std::shared_mutex> lock(software_map_mutex_);
				software_map_.ready = true;
			}
			save_software_map();
			update_software_map_markdown();

			std::this_thread::sleep_for(std::chrono::seconds(5));
			continue;
		}

		software_map_symbol target_sym;
		size_t target_idx = (size_t)-1;

		{
			std::unique_lock<std::shared_mutex> lock(software_map_mutex_);
			if (software_map_.symbols.empty()) continue;

			// Find the highest priority unsampled symbol
			int best_score = -1;
			for (size_t i = 0; i < software_map_.symbols.size(); ++i) {
				if (!software_map_.symbols[i].is_sampled && software_map_.symbols[i].accumulated_count > best_score) {
					best_score = software_map_.symbols[i].accumulated_count;
					target_idx = i;
				}
			}

			if (target_idx == (size_t)-1) {
				// All symbols sampled. We could reset them all if there was file churn, 
				// but for now we'll just wait.
				std::this_thread::sleep_for(std::chrono::seconds(5));
				continue;
			}

			target_sym = software_map_.symbols[target_idx];
			software_map_.symbols[target_idx].is_sampled = true; // Mark as sampled immediately so we don't pick it again if we fail
		}

		// Perform queries OUTSIDE the lock
		auto refs = lsp_query_references(target_sym.location.path, target_sym.location.range.start_y, target_sym.location.range.start_x);
		int inbound_count = refs.size();

		auto outgoing = lsp_query_call_hierarchy_outgoing(target_sym.location.path, target_sym.location.range.start_y, target_sym.location.range.start_x);

		std::vector<lsp_manager::type_hierarchy_item> supertypes;
		if (target_sym.kind == 5 || target_sym.kind == 22 || target_sym.kind == 11) {
			supertypes = lsp_query_type_hierarchy_supertypes(target_sym.location.path, target_sym.location.range.start_y, target_sym.location.range.start_x);
		}

		// Update stats INSIDE the lock
		{
			std::unique_lock<std::shared_mutex> lock(software_map_mutex_);
			// Ensure index is still valid (in case cache was reset)
			if (target_idx < software_map_.symbols.size() && software_map_.symbols[target_idx].name == target_sym.name) {
				software_map_.symbols[target_idx].looked_up_count = inbound_count;
				// Boost accumulated count by exact inbound count once we verify it
				software_map_.symbols[target_idx].accumulated_count += inbound_count;

				if (!supertypes.empty()) {
					std::string bases;
					for (size_t i = 0; i < supertypes.size(); ++i) {
						bases += supertypes[i].name;
						if (i < supertypes.size() - 1) bases += ", ";

						// Propagate Elo-style importance upwards to base classes
						auto it = software_map_.name_to_indices.find(supertypes[i].name);
						if (it != software_map_.name_to_indices.end()) {
							for (size_t idx : it->second) {
								auto &base_sym = software_map_.symbols[idx];
								if (base_sym.location.path == supertypes[i].uri || supertypes[i].uri.ends_with(base_sym.location.path)) {
									base_sym.accumulated_count += 3; // Extra weight for being a base class
									break;
								}
							}
						}
					}
					software_map_.symbols[target_idx].base_classes = bases;
				}
			}

			// Propagate outbound importance
			std::string build_dir_str = "/" + config_manager::get_instance().get_build_directory() + "/";
			for (const auto &out_call : outgoing) {
				if (out_call.uri.find(build_dir_str) != std::string::npos || out_call.uri.find("/build") != std::string::npos || out_call.uri.starts_with("/usr/"))
					continue;
					
				auto it = software_map_.name_to_indices.find(out_call.name);
				if (it != software_map_.name_to_indices.end()) {
					for (size_t idx : it->second) {
						auto &sym = software_map_.symbols[idx];
						if (sym.location.path == out_call.uri || out_call.uri.ends_with(sym.location.path)) {
							sym.accumulated_count++;
							break; // Found it, move to next outgoing call
						}
					}
				}
			}
		}

		// Periodically save
		if (++sample_counter >= 20) {
			sample_counter = 0;
			save_software_map();
			update_software_map_markdown();
		}

		// Rate limit sampling
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
	}
}
