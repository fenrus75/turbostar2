#include "config_manager.h"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include "event_logger.h"

namespace fs = std::filesystem;

config_manager &config_manager::get_instance()
{
	static config_manager instance;
	return instance;
}

std::string config_manager::get_config_file_path() const
{
	const char *home = getenv("HOME");
	if (home) {
		return std::string(home) + "/.turbostar";
	}
	return ".turbostar";
}

void config_manager::load()
{
	// 1. Load global config
	std::string global_path = get_config_file_path();
	load_from_file(global_path);

	// 2. Load project config if available (we determine this later or let main trigger it)
	// For now, load() just loads global. main.cpp will orchestrate project overlay after git_manager is ready.
}

void config_manager::load_from_file(const std::string &path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);

		// Trim whitespace
		key.erase(0, key.find_first_not_of(" \t"));
		key.erase(key.find_last_not_of(" \t") + 1);
		value.erase(0, value.find_first_not_of(" \t"));
		value.erase(value.find_last_not_of(" \t") + 1);

		if (key == "clang_format_style") {
			clang_format_style_ = value;
		} else if (key == "build_system") {
			build_system_ = value;
		} else if (key == "build_directory") {
			build_directory_ = value;
		} else if (key == "lsp_enabled") {
			lsp_enabled_ = (value == "true" || value == "1");
		} else if (key == "auto_open_error_files") {
			auto_open_error_files_ = (value == "true" || value == "1");
		} else if (key == "compile_on_save") {
			compile_on_save_ = (value == "true" || value == "1");
		} else if (key == "software_map_enabled") {
			software_map_enabled_ = (value == "true" || value == "1");
		} else if (key == "default_model_id" || key == "llm_url") {
			default_model_id_ = value;
		} else if (key == "log_all_tool_calls") {
			log_all_tool_calls_ = (value == "true" || value == "1");
		} else if (key == "main_executable") {
			main_executable_ = value;
		} else if (key == "run_arguments") {
			run_arguments_ = value;
		} else if (key == "run_target_mode") {
			run_target_mode_ = value;
		} else if (key == "gdb_auto_continue") {
			gdb_auto_continue_ = (value == "true" || value == "1");
		}
	}
	event_logger::get_instance().log("Configuration loaded from {}", path);
}

void config_manager::save_global()
{
	std::string path = get_config_file_path();
	save_project(path); // Re-use the logic but with the global path
}

void config_manager::save_project(const std::string &target_path)
{
	std::string path = target_path;

	// If it's a directory (like a repo root), append config.ini
	if (fs::is_directory(path) || (!fs::exists(path) && path.find(".turbostar") == std::string::npos && path.find("config.ini") == std::string::npos)) {
		path = fs::path(path) / "config.ini";
	}

	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save configuration to {}", path);
		return;
	}

	file << "# Turbostar Configuration File\n";
	file << "clang_format_style=" << clang_format_style_ << "\n";
	file << "build_system=" << build_system_ << "\n";
	file << "build_directory=" << build_directory_ << "\n";
	file << "default_model_id=" << default_model_id_ << "\n";
	file << "lsp_enabled=" << (lsp_enabled_ ? "true" : "false") << "\n";
	file << "auto_open_error_files=" << (auto_open_error_files_ ? "true" : "false") << "\n";
	file << "compile_on_save=" << (compile_on_save_ ? "true" : "false") << "\n";
	file << "software_map_enabled=" << (software_map_enabled_ ? "true" : "false") << "\n";
	file << "paranoid_mode=" << (paranoid_mode_ ? "true" : "false") << "\n";
	file << "log_all_tool_calls=" << (log_all_tool_calls_ ? "true" : "false") << "\n";
	file << "main_executable=" << main_executable_ << "\n";
	file << "run_arguments=" << run_arguments_ << "\n";
	file << "run_target_mode=" << run_target_mode_ << "\n";
	file << "gdb_auto_continue=" << (gdb_auto_continue_ ? "true" : "false") << "\n";

	event_logger::get_instance().log("Configuration saved to {}", path);
}
