#include "statistics_manager.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdlib>

statistics_manager &statistics_manager::get_instance()
{
	static statistics_manager instance;
	return instance;
}

void statistics_manager::increment_stat(const std::string &key, int amount)
{
	std::lock_guard<std::mutex> lock(mutex_);
	stats_[key] += amount;

	// Automatically persist the updated metrics to ensure that usage telemetry
	// is not lost in case of editor crash or abnormal exit.
	save_unlocked();
}

int statistics_manager::get_stat(const std::string &key) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = stats_.find(key);
	if (it != stats_.end()) {
		return it->second;
	}
	return 0;
}

std::map<std::string, int> statistics_manager::get_all_stats() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return stats_;
}

void statistics_manager::load()
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string filepath = get_stats_file_path();
	if (!std::filesystem::exists(filepath)) {
		return;
	}

	try {
		std::ifstream file(filepath);
		if (file.is_open()) {
			nlohmann::json root;
			file >> root;
			stats_ = root.get<std::map<std::string, int>>();
		}
	} catch (const std::exception &e) {
		std::cerr << "Failed to parse statistics.json: " << e.what() << std::endl;
	}
}

void statistics_manager::save() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	save_unlocked();
}

void statistics_manager::save_unlocked() const
{
	std::string filepath = get_stats_file_path();
	try {
		nlohmann::json root = stats_;
		std::ofstream file(filepath);
		if (file.is_open()) {
			file << root.dump(4);
		}
	} catch (const std::exception &e) {
		std::cerr << "Failed to save statistics.json: " << e.what() << std::endl;
	}
}

std::string statistics_manager::get_stats_file_path() const
{
	const char *home = std::getenv("HOME");
	std::string base_dir = home ? std::string(home) + "/.cache/turbostar" : ".cache/turbostar";
	try {
		std::filesystem::create_directories(base_dir);
	} catch (...) {
		// Ignore directory creation failure fallback
	}
	return base_dir + "/statistics.json";
}
