#include "mcp_manager.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../config_manager.h"
#include "../event_logger.h"

namespace fs = std::filesystem;

namespace agentlib
{

mcp_manager &mcp_manager::get_instance()
{
	static mcp_manager instance;
	return instance;
}

void mcp_manager::discover_and_load(const std::string &project_root)
{
	servers_.clear();

	// 1. Discover System MCPs
	std::vector<std::string> system_paths;
	const char *home = getenv("HOME");
	if (home) {
		std::string home_str(home);
		system_paths.push_back(home_str + "/.claude/mcp.json");
		system_paths.push_back(home_str + "/.copilot/mcp-config.json");
		system_paths.push_back(home_str + "/.gemini/config/mcp_config.json");
	}

	for (const auto &path : system_paths) {
		load_servers_from_file(path, true);
	}

	// 2. Discover Project MCPs
	if (!project_root.empty()) {
		std::string project_path = project_root + "/.agents/mcp_config.json";
		load_servers_from_file(project_path, false);
	}
}

std::shared_ptr<mcp_server> mcp_manager::find_server(const std::string &name) const
{
	for (const auto &server : servers_) {
		if (server->get_name() == name) {
			return server;
		}
	}
	return nullptr;
}

void mcp_manager::save_configs(const std::string &project_root)
{
	// Sync the manager's memory states to config_manager
	auto &cfg = config_manager::get_instance();
	for (const auto &server : servers_) {
		cfg.set_mcp_server_enabled(server->get_name(), server->is_system(), server->is_enabled());
		for (const auto &tool : server->get_tools()) {
			cfg.set_mcp_tool_enabled(server->get_name(), tool.name, server->is_system(), tool.enabled);
		}
	}

	// Persist the actual config files
	cfg.save_global();
	if (!project_root.empty()) {
		cfg.save_project(project_root);
	}
}

void mcp_manager::load_servers_from_file(const std::string &path, bool is_system)
{
	if (!fs::exists(path)) {
		return;
	}

	std::ifstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to open MCP config file: {}", path);
		return;
	}

	try {
		nlohmann::json root = nlohmann::json::parse(file);
		if (!root.contains("mcpServers") || !root["mcpServers"].is_object()) {
			return;
		}

		auto &cfg = config_manager::get_instance();

		for (const auto &[name, server_json] : root["mcpServers"].items()) {
			if (!server_json.is_object()) {
				continue;
			}

			auto server = std::make_shared<mcp_server>();
			server->set_name(name);
			server->set_system(is_system);

			if (server_json.contains("command") && server_json["command"].is_string()) {
				server->set_command(server_json["command"].get<std::string>());
			}
			if (server_json.contains("args") && server_json["args"].is_array()) {
				std::vector<std::string> args;
				for (const auto &arg : server_json["args"]) {
					if (arg.is_string()) {
						args.push_back(arg.get<std::string>());
					}
				}
				server->set_args(args);
			}
			if (server_json.contains("env") && server_json["env"].is_object()) {
				std::map<std::string, std::string> env;
				for (const auto &[env_key, env_val] : server_json["env"].items()) {
					if (env_val.is_string()) {
						env[env_key] = env_val.get<std::string>();
					}
				}
				server->set_env(env);
			}

			server->auto_detect_type();

			// Read enabled status from config with priority-based defaults (system = true, project = false)
			bool default_enabled = is_system;
			bool enabled = cfg.is_mcp_server_enabled(name, is_system, default_enabled);
			server->set_enabled(enabled);

			// Conflict resolution
			auto existing = find_server(name);
			if (existing) {
				if (existing->is_system() && !is_system) {
					// Local project server definition overrides global system definition with a twist:
					// If global one is enabled and local is disabled, the global one still wins and remains active.
					bool global_enabled = existing->is_enabled();
					bool local_enabled = enabled;
					if (global_enabled && !local_enabled) {
						event_logger::get_instance().log(
						    "MCP Conflict: Keeping global server '{}' over disabled project server.", name);
						continue;
					} else {
						// Overwrite system with project definition
						servers_.erase(std::remove(servers_.begin(), servers_.end(), existing), servers_.end());
						servers_.push_back(server);
						event_logger::get_instance().log(
						    "MCP Conflict: Overriding global server '{}' with project definition.", name);
					}
				} else {
					// Duplicate in same category: replace
					servers_.erase(std::remove(servers_.begin(), servers_.end(), existing), servers_.end());
					servers_.push_back(server);
				}
			} else {
				servers_.push_back(server);
			}
		}

		event_logger::get_instance().log("Discovered and loaded MCP servers from: {}", path);

	} catch (const std::exception &e) {
		event_logger::get_instance().log("Error parsing MCP config file '{}': {}", path, e.what());
	}
}

} // namespace agentlib
