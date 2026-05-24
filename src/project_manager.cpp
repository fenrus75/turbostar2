#include "project_manager.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include "config_manager.h"
#include "crashdump_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"

namespace fs = std::filesystem;

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
