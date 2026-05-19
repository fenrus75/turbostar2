#include "history_manager.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include "event_logger.h"

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
			}
		}
	}
	event_logger::get_instance().log("Loaded history: " + std::to_string(searches_.size()) + " searches, " +
					 std::to_string(files_.size()) + " files.");
}

void history_manager::save() const
{
	std::string path = get_history_file_path();
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save history to " + path);
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
	for (const auto &[path, pos] : cursor_memory_) {
		file << path << " " << pos.x << " " << pos.y << "\n";
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

	// Remove if exists to bring to front
	auto it = std::find(files_.begin(), files_.end(), filename);
	if (it != files_.end()) {
		files_.erase(it);
	}

	files_.push_front(filename);

	if (files_.size() > max_history_items_) {
		files_.pop_back();
	}
}
void history_manager::set_cursor_pos(const std::string &filename, int x, int y)
{
	if (!filename.empty() && filename != "unknown.txt") {
		cursor_memory_[filename] = {x, y};
	}
}

std::optional<cursor_pos> history_manager::get_cursor_pos(const std::string &filename) const
{
	auto it = cursor_memory_.find(filename);
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
