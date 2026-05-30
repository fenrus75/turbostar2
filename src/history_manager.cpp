#include "history_manager.h"
#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>
#include "event_logger.h"
#include "fs_utils.h"

history_manager &history_manager::get_instance()
{
	static history_manager instance;
	return instance;
}

std::string history_manager::get_history_file_path() const
{
	const char *home = getenv("HOME");
	if (home) {
		return std::string(home) + "/.turbostar_history";
	}
	return ".turbostar_history";
}

void history_manager::load()
{
	searches_.clear();
	files_.clear();
	cursor_memory_.clear();
	cursor_lru_.clear();
	project_files_.clear();

	std::string path = get_history_file_path();
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}

	std::string line;
	std::string current_section;

	while (std::getline(file, line)) {
		// Trim whitespace (simple approach)
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (line.empty() || line[0] == '#') {
			continue;
		}

		if (line[0] == '[' && line.back() == ']') {
			current_section = line.substr(1, line.length() - 2);
		} else {
			if (current_section == "search") {
				searches_.push_back(line);
			} else if (current_section == "files") {
				files_.push_back(line);
			} else if (current_section == "cursor_memory") {
				std::stringstream ss(line);
				int x, y;
				if (ss >> x >> y) {
					std::string filepath;
					std::getline(ss, filepath);
					// Trim leading space from getline
					if (!filepath.empty() && filepath[0] == ' ') {
						filepath.erase(0, 1);
					}
					if (!filepath.empty()) {
						cursor_memory_[filepath] = {x, y};
						cursor_lru_.push_back(filepath);
					}
				}
			} else if (current_section == "projects") {
				size_t pipe = line.find('|');
				if (pipe != std::string::npos) {
					std::string proj = line.substr(0, pipe);
					std::string f = line.substr(pipe + 1);
					project_files_[proj].push_back(f);
				}
			}
		}
	}
	event_logger::get_instance().log("Loaded history: {} searches, {} files, {} cursor positions.",
					 searches_.size(), files_.size(), cursor_memory_.size());
}

void history_manager::save() const
{
	std::string path = get_history_file_path();
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save history to {}", path);
		return;
	}

	file << "[search]\n";
	for (const auto &s : searches_) {
		file << s << "\n";
	}

	file << "\n[files]\n";
	for (const auto &f : files_) {
		file << f << "\n";
	}

	file << "\n[cursor_memory]\n";
	// Save in LRU order (oldest to newest is fine for loading)
	for (const auto &p : cursor_lru_) {
		auto it = cursor_memory_.find(p);
		if (it != cursor_memory_.end()) {
			file << it->second.x << " " << it->second.y << " " << p << "\n";
		}
	}

	file << "\n[projects]\n";
	for (const auto &[proj, open_files] : project_files_) {
		for (const auto &f : open_files) {
			file << proj << "|" << f << "\n";
		}
	}
}

void history_manager::add_search(const std::string &search_string)
{
	if (search_string.empty())
		return;

	// Remove if exists to bring to front
	auto it = std::find(searches_.begin(), searches_.end(), search_string);
	if (it != searches_.end()) {
		searches_.erase(it);
	}

	searches_.push_front(search_string);

	if (searches_.size() > max_history_items_) {
		searches_.pop_back();
	}
}

void history_manager::add_file(const std::string &filename)
{
	if (filename.empty() || filename == "unknown.txt")
		return;

	std::string abs_path = fs_utils::safe_absolute(filename).string();

	// Remove if exists to bring to front
	auto it = std::find(files_.begin(), files_.end(), abs_path);
	if (it != files_.end()) {
		files_.erase(it);
	}

	files_.push_front(abs_path);

	if (files_.size() > max_history_items_) {
		files_.pop_back();
	}
}
void history_manager::set_cursor_pos(const std::string &filename, int x, int y)
{
	if (filename.empty() || filename == "unknown.txt")
		return;

	std::string abs_path = fs_utils::safe_absolute(filename).string();

	// Update LRU
	auto it = std::find(cursor_lru_.begin(), cursor_lru_.end(), abs_path);
	if (it != cursor_lru_.end()) {
		cursor_lru_.erase(it);
	}
	cursor_lru_.push_front(abs_path);

	cursor_memory_[abs_path] = {x, y};

	// Evict if over limit
	while (cursor_lru_.size() > max_cursor_memory_) {
		std::string oldest = cursor_lru_.back();
		cursor_lru_.pop_back();
		cursor_memory_.erase(oldest);
	}
}

std::optional<cursor_pos> history_manager::get_cursor_pos(const std::string &filename) const
{
	if (filename.empty() || filename == "unknown.txt")
		return std::nullopt;

	std::string abs_path = fs_utils::safe_absolute(filename).string();

	auto it = cursor_memory_.find(abs_path);
	if (it != cursor_memory_.end()) {
		return it->second;
	}
	return std::nullopt;
}

void history_manager::set_project_files(const std::string &project_root, const std::vector<std::string> &files)
{
	if (!project_root.empty()) {
		project_files_[project_root] = files;
	}
}

std::vector<std::string> history_manager::get_project_files(const std::string &project_root) const
{
	auto it = project_files_.find(project_root);
	if (it != project_files_.end()) {
		return it->second;
	}
	return {};
}
