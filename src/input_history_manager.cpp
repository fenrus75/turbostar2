#include "input_history_manager.h"
#include "project_manager.h"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include "event_logger.h"

input_history_manager &input_history_manager::get_instance()
{
	static input_history_manager instance;
	return instance;
}

std::string input_history_manager::get_history_file_path() const
{
	std::string root = project_manager::get_instance().get_project_root();
	if (!root.empty()) {
		return root + "/.turbostar_input_history.json";
	}
	const char *home = getenv("HOME");
	if (home) {
		return std::string(home) + "/.turbostar_input_history.json";
	}
	return ".turbostar_input_history.json";
}

void input_history_manager::load()
{
	histories_.clear();
	std::string path = get_history_file_path();
	std::ifstream file(path);
	if (!file.is_open()) {
		return;
	}
	try {
		nlohmann::json j;
		file >> j;
		for (auto &el : j.items()) {
			std::vector<std::string> history = el.value().get<std::vector<std::string>>();
			histories_[el.key()] = history;
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to load input history: {}", e.what());
	}
}

void input_history_manager::save()
{
	std::string path = get_history_file_path();
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save input history to {}", path);
		return;
	}
	try {
		nlohmann::json j = nlohmann::json::object();
		for (const auto &[key, val] : histories_) {
			j[key] = val;
		}
		file << j.dump(4);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to serialize input history: {}", e.what());
	}
}

void input_history_manager::add_entry(const std::string &history_id, const std::string &entry)
{
	if (entry.empty()) {
		return;
	}
	auto &history = histories_[history_id];
	// Avoid consecutive duplicates
	if (!history.empty() && history.back() == entry) {
		return;
	}
	// Deduplicate: move existing to the end
	auto it = std::find(history.begin(), history.end(), entry);
	if (it != history.end()) {
		history.erase(it);
	}
	history.push_back(entry);
	if (history.size() > 64) {
		history.erase(history.begin());
	}
	save();
}

const std::vector<std::string> &input_history_manager::get_history(const std::string &history_id)
{
	return histories_[history_id];
}
