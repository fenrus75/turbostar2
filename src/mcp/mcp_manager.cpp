#include "mcp_manager.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../agentlib/tool_registry.h"
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

mcp_manager::~mcp_manager()
{
	if (startup_thread_.joinable()) {
		startup_thread_.join();
	}
}

void mcp_manager::discover_and_load(const std::string &project_root)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
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

std::vector<std::shared_ptr<mcp_server>> mcp_manager::get_servers() const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	return servers_;
}

std::shared_ptr<mcp_server> mcp_manager::find_server(const std::string &name) const
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (const auto &server : servers_) {
		if (server->get_name() == name) {
			return server;
		}
	}
	return nullptr;
}

void mcp_manager::save_configs(const std::string &project_root)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto &cfg = config_manager::get_instance();
	for (const auto &server : servers_) {
		cfg.set_mcp_server_enabled(server->get_name(), server->is_system(), server->is_enabled());
		for (const auto &tool : server->get_tools()) {
			cfg.set_mcp_tool_enabled(server->get_name(), tool.name, server->is_system(), tool.enabled);
		}
	}

	cfg.save_global();
	if (!project_root.empty()) {
		cfg.save_project(project_root);
	}
}

void mcp_manager::start_async(const std::string &project_root)
{
	if (startup_thread_.joinable()) {
		startup_thread_.join();
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	startup_thread_ = std::thread([this, project_root]() {
		discover_and_load(project_root);
		start_active_servers();
	});
}

void mcp_manager::start_active_servers()
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto &server : servers_) {
		if (server->is_enabled()) {
			if (server->start()) {
				for (const auto &tool : server->get_tools()) {
					if (tool.enabled) {
						std::string server_name = server->get_name();
						bool is_system = server->is_system();
						tool_registry::get_instance().register_validator([server_name, tool, is_system]() {
							return std::make_unique<mcp_tool_validator>(server_name, tool, is_system);
						});
					}
				}
			}
		}
	}
}

void mcp_manager::stop_all_servers()
{
	if (startup_thread_.joinable()) {
		startup_thread_.join();
	}
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	for (auto &server : servers_) {
		server->stop();
		for (const auto &tool : server->get_tools()) {
			tool_registry::get_instance().unregister_validator("mcp:" + server->get_name() + ":" + tool.name);
		}
	}
}

void mcp_manager::toggle_server(const std::string &name, bool enable)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto server = find_server(name);
	if (!server) {
		return;
	}

	if (enable == server->is_enabled()) {
		return;
	}

	server->set_enabled(enable);
	if (enable) {
		if (server->start()) {
			for (const auto &tool : server->get_tools()) {
				if (tool.enabled) {
					std::string server_name = server->get_name();
					bool is_system = server->is_system();
					tool_registry::get_instance().register_validator([server_name, tool, is_system]() {
						return std::make_unique<mcp_tool_validator>(server_name, tool, is_system);
					});
				}
			}
		}
	} else {
		server->stop();
		for (const auto &tool : server->get_tools()) {
			tool_registry::get_instance().unregister_validator("mcp:" + server->get_name() + ":" + tool.name);
		}
	}
}

void mcp_manager::toggle_tool(const std::string &server_name, const std::string &tool_name, bool enable)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	auto server = find_server(server_name);
	if (!server) {
		return;
	}

	auto tools = server->get_tools();
	for (auto &tool : tools) {
		if (tool.name == tool_name) {
			if (tool.enabled == enable) {
				return;
			}
			tool.enabled = enable;
			server->set_tools(tools);

			std::string reg_name = "mcp:" + server_name + ":" + tool_name;
			if (enable && server->is_running()) {
				bool is_system = server->is_system();
				tool_registry::get_instance().register_validator([server_name, tool, is_system]() {
					return std::make_unique<mcp_tool_validator>(server_name, tool, is_system);
				});
			} else {
				tool_registry::get_instance().unregister_validator(reg_name);
			}
			break;
		}
	}
}

void mcp_manager::load_servers_from_file(const std::string &path, bool is_system)
{
	std::lock_guard<std::recursive_mutex> lock(mutex_);
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

			bool default_enabled = is_system;
			bool enabled = cfg.is_mcp_server_enabled(name, is_system, default_enabled);
			server->set_enabled(enabled);

			auto existing = find_server(name);
			if (existing) {
				if (existing->is_system() && !is_system) {
					bool global_enabled = existing->is_enabled();
					bool local_enabled = enabled;
					if (global_enabled && !local_enabled) {
						event_logger::get_instance().log(
						    "MCP Conflict: Keeping global server '{}' over disabled project server.", name);
						continue;
					} else {
						servers_.erase(std::remove(servers_.begin(), servers_.end(), existing), servers_.end());
						servers_.push_back(server);
						event_logger::get_instance().log(
						    "MCP Conflict: Overriding global server '{}' with project definition.", name);
					}
				} else {
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
