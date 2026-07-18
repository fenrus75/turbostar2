#include "config_manager.h"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include "event_logger.h"
#include "project_manager.h"

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
		} else if (key == "shell_display_access") {
			shell_display_access_ = (value == "true" || value == "1");
		} else if (key == "main_executable") {
			main_executable_ = value;
		} else if (key == "github_access_token") {
			bool is_project = (path.find(".turbostar") == std::string::npos);
			if (!is_project || !value.empty()) {
				github_access_token_ = value;
			}
		} else if (key == "run_arguments") {
			run_arguments_ = value;
		} else if (key == "run_target_mode") {
			run_target_mode_ = value;
		} else if (key == "gdb_auto_continue") {
			gdb_auto_continue_ = (value == "true" || value == "1");
		} else if (key == "last_search_query") {
			last_search_query_ = value;
		} else if (key == "last_replace_query") {
			last_replace_query_ = value;
		} else if (key == "tab_width") {
			try {
				tab_width_ = std::stoi(value);
			} catch (...) {
			}
		} else if (key == "max_line_width") {
			try {
				max_line_width_ = std::stoi(value);
			} catch (...) {
			}
		} else if (key.starts_with("family.")) {
			bool is_project = (path.find(".turbostar") == std::string::npos);
			size_t dot1 = 7; // length of "family."
			size_t dot2 = key.find('.', dot1);
			if (dot2 != std::string::npos) {
				std::string family_name = key.substr(dot1, dot2 - dot1);
				std::string subkey = key.substr(dot2 + 1);
				if (subkey == "enabled") {
					if (is_project) {
						project_tool_families_enabled_[family_name] = (value == "true" || value == "1");
					} else {
						tool_families_enabled_[family_name] = (value == "true" || value == "1");
					}
				}
			}
		} else if (key.starts_with("mcp.")) {
			bool is_project = (path.find(".turbostar") == std::string::npos);
			size_t dot1 = 4; // length of "mcp."
			size_t dot2 = key.find('.', dot1);
			if (dot2 != std::string::npos) {
				std::string server_name = key.substr(dot1, dot2 - dot1);
				std::string subkey = key.substr(dot2 + 1);
				if (subkey == "enabled") {
					if (is_project) {
						project_mcp_servers_enabled_[server_name] = (value == "true" || value == "1");
					} else {
						mcp_servers_enabled_[server_name] = (value == "true" || value == "1");
					}
				} else if (subkey == "when_to_activate") {
					if (is_project) {
						project_mcp_servers_when_to_activate_[server_name] = value;
					} else {
						mcp_servers_when_to_activate_[server_name] = value;
					}
				} else if (subkey.ends_with(".enabled")) {
					std::string tool_name = subkey.substr(0, subkey.length() - 8);
					if (is_project) {
						project_mcp_tools_enabled_[server_name + ":" + tool_name] =
						    (value == "true" || value == "1");
					} else {
						mcp_tools_enabled_[server_name + ":" + tool_name] = (value == "true" || value == "1");
					}
				}
			}
		} else if (key.starts_with("task.")) {
			size_t dot1 = 5; // length of "task."
			size_t dot2 = key.find('.', dot1);
			if (dot2 != std::string::npos) {
				std::string task_id = key.substr(dot1, dot2 - dot1);
				std::string subkey = key.substr(dot2 + 1);
				if (subkey == "model") {
					task_models_[task_id] = value;
				}
			}
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
	if (fs::is_directory(path) ||
	    (!fs::exists(path) && path.find(".turbostar") == std::string::npos && path.find("config.ini") == std::string::npos)) {
		path = fs::path(path) / "config.ini";
	}

	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to save configuration to {}", path);
		return;
	}

	bool is_project = (target_path.find(".turbostar") == std::string::npos);

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
	file << "shell_display_access=" << (shell_display_access_ ? "true" : "false") << "\n";
	file << "main_executable=" << main_executable_ << "\n";
	if (!is_project) {
		file << "github_access_token=" << github_access_token_ << "\n";
	}
	file << "run_arguments=" << run_arguments_ << "\n";
	file << "run_target_mode=" << run_target_mode_ << "\n";
	file << "gdb_auto_continue=" << (gdb_auto_continue_ ? "true" : "false") << "\n";
	file << "tab_width=" << tab_width_ << "\n";
	file << "max_line_width=" << max_line_width_ << "\n";
	file << "last_search_query=" << last_search_query_ << "\n";
	file << "last_replace_query=" << last_replace_query_ << "\n";

	for (const auto &[task_id, model_id] : task_models_) {
		if (!model_id.empty()) {
			file << "task." << task_id << ".model=" << model_id << "\n";
		}
	}

	if (is_project) {
		for (const auto &[server, enabled] : project_mcp_servers_enabled_) {
			file << "mcp." << server << ".enabled=" << (enabled ? "true" : "false") << "\n";
		}
		for (const auto &[server, text] : project_mcp_servers_when_to_activate_) {
			if (!text.empty()) {
				file << "mcp." << server << ".when_to_activate=" << text << "\n";
			}
		}
		for (const auto &[key_pair, enabled] : project_mcp_tools_enabled_) {
			size_t colon = key_pair.find(':');
			if (colon != std::string::npos) {
				std::string server = key_pair.substr(0, colon);
				std::string tool = key_pair.substr(colon + 1);
				file << "mcp." << server << "." << tool << ".enabled=" << (enabled ? "true" : "false") << "\n";
			}
		}
		for (const auto &[family, enabled] : project_tool_families_enabled_) {
			file << "family." << family << ".enabled=" << (enabled ? "true" : "false") << "\n";
		}
	} else {
		for (const auto &[server, enabled] : mcp_servers_enabled_) {
			file << "mcp." << server << ".enabled=" << (enabled ? "true" : "false") << "\n";
		}
		for (const auto &[server, text] : mcp_servers_when_to_activate_) {
			if (!text.empty()) {
				file << "mcp." << server << ".when_to_activate=" << text << "\n";
			}
		}
		for (const auto &[key_pair, enabled] : mcp_tools_enabled_) {
			size_t colon = key_pair.find(':');
			if (colon != std::string::npos) {
				std::string server = key_pair.substr(0, colon);
				std::string tool = key_pair.substr(colon + 1);
				file << "mcp." << server << "." << tool << ".enabled=" << (enabled ? "true" : "false") << "\n";
			}
		}
		for (const auto &[family, enabled] : tool_families_enabled_) {
			file << "family." << family << ".enabled=" << (enabled ? "true" : "false") << "\n";
		}
	}

	event_logger::get_instance().log("Configuration saved to {}", path);
}

bool config_manager::is_mcp_server_enabled(const std::string &server_name, bool is_system, bool default_val) const
{
	if (is_system) {
		auto it = mcp_servers_enabled_.find(server_name);
		if (it != mcp_servers_enabled_.end()) {
			return it->second;
		}
	} else {
		auto it = project_mcp_servers_enabled_.find(server_name);
		if (it != project_mcp_servers_enabled_.end()) {
			return it->second;
		}
	}
	return default_val;
}

void config_manager::set_mcp_server_enabled(const std::string &server_name, bool is_system, bool enabled)
{
	if (is_system) {
		mcp_servers_enabled_[server_name] = enabled;
	} else {
		project_mcp_servers_enabled_[server_name] = enabled;
	}
}

bool config_manager::is_mcp_tool_enabled(const std::string &server_name, const std::string &tool_name, bool is_system,
					 bool default_val) const
{
	std::string key = server_name + ":" + tool_name;
	if (is_system) {
		auto it = mcp_tools_enabled_.find(key);
		if (it != mcp_tools_enabled_.end()) {
			return it->second;
		}
	} else {
		auto it = project_mcp_tools_enabled_.find(key);
		if (it != project_mcp_tools_enabled_.end()) {
			return it->second;
		}
	}
	return default_val;
}

void config_manager::set_mcp_tool_enabled(const std::string &server_name, const std::string &tool_name, bool is_system, bool enabled)
{
	std::string key = server_name + ":" + tool_name;
	if (is_system) {
		mcp_tools_enabled_[key] = enabled;
	} else {
		project_mcp_tools_enabled_[key] = enabled;
	}
}

bool config_manager::is_tool_family_enabled(const std::string &family_name, bool is_system, bool default_val) const
{
	if (family_name == "base") {
		return true; // base is always enabled
	}
	if (is_system) {
		auto it = tool_families_enabled_.find(family_name);
		if (it != tool_families_enabled_.end()) {
			return it->second;
		}
	} else {
		auto it = project_tool_families_enabled_.find(family_name);
		if (it != project_tool_families_enabled_.end()) {
			return it->second;
		}
	}
	return default_val;
}

void config_manager::set_tool_family_enabled(const std::string &family_name, bool is_system, bool enabled)
{
	if (family_name == "base") {
		return; // base is always enabled
	}
	if (is_system) {
		tool_families_enabled_[family_name] = enabled;
	} else {
		project_tool_families_enabled_[family_name] = enabled;
	}
}

std::string config_manager::get_mcp_server_when_to_activate(const std::string &server_name, bool is_system) const
{
	if (is_system) {
		auto it = mcp_servers_when_to_activate_.find(server_name);
		if (it != mcp_servers_when_to_activate_.end()) {
			return it->second;
		}
	} else {
		auto it = project_mcp_servers_when_to_activate_.find(server_name);
		if (it != project_mcp_servers_when_to_activate_.end()) {
			return it->second;
		}
	}
	return "";
}

void config_manager::set_mcp_server_when_to_activate(const std::string &server_name, bool is_system, const std::string &text)
{
	if (is_system) {
		mcp_servers_when_to_activate_[server_name] = text;
	} else {
		project_mcp_servers_when_to_activate_[server_name] = text;
	}
}

std::string config_manager::get_task_model_id(const std::string &task_id) const
{
	auto it = task_models_.find(task_id);
	if (it != task_models_.end() && !it->second.empty()) {
		return it->second;
	}
	return default_model_id_;
}

void config_manager::set_task_model_id(const std::string &task_id, const std::string &model_id)
{
	task_models_[task_id] = model_id;
}

void config_manager::set_tab_width(int width)
{
	tab_width_ = width;
}

void config_manager::read_clang_format(const std::string &project_root)
{
	std::string path = (fs::path(project_root) / ".clang-format").string();
	std::ifstream file(path);
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#' || line[0] == ';')
			continue;

		size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;

		std::string key = line.substr(0, colon);
		std::string val = line.substr(colon + 1);

		// Trim key and val
		key.erase(0, key.find_first_not_of(" \t"));
		key.erase(key.find_last_not_of(" \t") + 1);
		val.erase(0, val.find_first_not_of(" \t"));
		val.erase(val.find_last_not_of(" \t") + 1);

		if (key == "TabWidth") {
			try {
				int width = std::stoi(val);
				if (width > 0 && width <= 32) {
					set_tab_width(width);
				}
			} catch (...) {
			}
		} else if (key == "ColumnLimit") {
			try {
				int limit = std::stoi(val);
				if (limit > 0) {
					set_max_line_width(limit);
				}
			} catch (...) {
			}
		}
	}
}

std::string config_manager::get_main_executable() const
{
	const char *no_detect = std::getenv("TURBOSTAR_NO_AUTO_DETECT");
	if (no_detect && *no_detect) {
		return main_executable_;
	}
	if (main_executable_.empty()) {
		return auto_detect_main_executable();
	}
	return main_executable_;
}

std::string config_manager::auto_detect_main_executable() const
{
	std::string project_root = project_manager::get_instance().get_project_root();
	if (project_root.empty()) {
		return "";
	}

	fs::path meson_path = fs::path(project_root) / "meson.build";
	if (!fs::exists(meson_path)) {
		return "";
	}

	std::ifstream file(meson_path);
	if (!file.is_open()) {
		return "";
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	std::string content = buffer.str();

	// Regex to match executable('target_name', ...) or executable("target_name", ...)
	std::regex exec_rx(R"(executable\s*\(\s*['"]([^'"]+)['"])");
	auto words_begin = std::sregex_iterator(content.begin(), content.end(), exec_rx);
	auto words_end = std::sregex_iterator();

	for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
		std::smatch match = *i;
		std::string name = match[1].str();
		
		// Skip test and utility helper targets
		if (name.starts_with("test_") || name.find("test") != std::string::npos ||
			name.starts_with("unit_") || name == "agentcli_record" ||
			name == "agentcli_replay") {
			continue;
		}
		
		main_executable_ = name;
		return main_executable_;
	}

	// Fallback to the first found executable target if all were test/helpers
	if (words_begin != words_end) {
		main_executable_ = (*words_begin)[1].str();
		return main_executable_;
	}

	return "";
}
