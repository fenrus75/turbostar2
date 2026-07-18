#include "session_manager.h"
#include "fs_utils.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "event_logger.h"

namespace fs = std::filesystem;

session_manager &session_manager::get_instance()
{
	static session_manager instance;
	return instance;
}

std::string session_manager::get_session_file_path() const
{
	if (!fs_utils::get_project_dir().empty()) {
		std::string cache_root = fs_utils::get_project_cache_root();
		if (!cache_root.empty()) {
			return (fs::path(cache_root) / "session.json").string();
		}
	}
	const char *home = getenv("HOME");
	if (home) {
		fs::path global_dir = fs::path(home) / ".turbostar";
		try {
			fs::create_directories(global_dir);
		} catch (...) {}
		return (global_dir / "session.json").string();
	}
	return "session.json";
}

void session_manager::load()
{
	std::string path = get_session_file_path();
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}

	try {
		nlohmann::json j;
		file >> j;
		if (j.contains("search")) {
			auto js = j["search"];
			if (js.contains("last_query")) {
				last_search_query_ = js["last_query"].get<std::string>();
			}
			if (js.contains("last_replacement")) {
				last_replace_query_ = js["last_replacement"].get<std::string>();
			}
		}
		event_logger::get_instance().log("Session loaded from {}", path);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to parse session file: {}", e.what());
	}
}

void session_manager::save() const
{
	std::string path = get_session_file_path();
	// Create directory if not exists
	fs::path parent = fs::path(path).parent_path();
	if (!parent.empty()) {
		try {
			fs::create_directories(parent);
		} catch (...) {}
	}

	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save session to {}", path);
		return;
	}

	nlohmann::json j;
	j["search"]["last_query"] = last_search_query_;
	j["search"]["last_replacement"] = last_replace_query_;

	file << j.dump(4) << std::endl;
	event_logger::get_instance().log("Session saved to {}", path);
}
