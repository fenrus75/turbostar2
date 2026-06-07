#include "mcp_manager.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../agentlib/tool_registry.h"
#include "../config_manager.h"
#include "../event_logger.h"
#include "../project_manager.h"
#include "../agentlib/ai_agent.h"
#include "../agentlib/ai_model.h"
#include "../agentlib/llm_client.h"
#include "../agentlib/httplib_transport.h"

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

	// Queue prompt generation for discovered servers that have tools
	for (const auto &server : servers_) {
		if (!server->get_tools().empty()) {
			queue_prompt_generation(server->get_name(), server->is_system());
		}
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
	std::vector<std::thread> start_threads;
	{
		std::lock_guard<std::recursive_mutex> lock(mutex_);
		for (auto &server : servers_) {
			if (server->is_enabled()) {
				start_threads.push_back(std::thread([this, server]() {
					if (project_manager::get_instance().is_exiting()) {
						return;
					}
					if (server->start()) {
						std::lock_guard<std::recursive_mutex> reg_lock(mutex_);
						bool has_tools = !server->get_tools().empty();
						for (const auto &tool : server->get_tools()) {
							if (tool.enabled) {
								std::string server_name = server->get_name();
								bool is_system = server->is_system();
								tool_registry::get_instance().register_validator([server_name, tool, is_system]() {
									return std::make_unique<mcp_tool_validator>(server_name, tool, is_system);
								});
							}
						}
						if (has_tools) {
							queue_prompt_generation(server->get_name(), server->is_system());
						}
					}
				}));
			}
		}
	}

	for (auto &t : start_threads) {
		if (t.joinable()) {
			t.join();
		}
	}
}

void mcp_manager::stop_all_servers()
{
	project_manager::get_instance().set_exiting(true);
	if (startup_thread_.joinable()) {
		startup_thread_.join();
	}
	{
		std::lock_guard<std::mutex> lock(prompt_mutex_);
		prompt_cv_.notify_all();
	}
	if (prompt_generation_thread_.joinable()) {
		prompt_generation_thread_.join();
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
			bool has_tools = !server->get_tools().empty();
			for (const auto &tool : server->get_tools()) {
				if (tool.enabled) {
					std::string server_name = server->get_name();
					bool is_system = server->is_system();
					tool_registry::get_instance().register_validator([server_name, tool, is_system]() {
						return std::make_unique<mcp_tool_validator>(server_name, tool, is_system);
					});
				}
			}
			if (has_tools) {
				queue_prompt_generation(server->get_name(), server->is_system());
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
			if (server->get_mcp_type() == "python") {
				server->statically_analyze_tools();
			}

			bool default_enabled = is_system;
			if (is_system && server->get_mcp_type() == "python") {
				std::string scan_output;
				if (!server->run_bandit_scan(scan_output)) {
					default_enabled = false;
					event_logger::get_instance().log(
					    "System MCP server '{}' has critical Bandit violations. Disabling by default.", name);
				}
			}
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

void mcp_manager::queue_prompt_generation(const std::string &server_name, bool is_system)
{
	std::lock_guard<std::recursive_mutex> reg_lock(mutex_);
	std::lock_guard<std::mutex> lock(prompt_mutex_);

	for (const auto &p : prompt_generation_queue_) {
		if (p.first == server_name) {
			return;
		}
	}

	auto server = find_server(server_name);
	bool enabled = server && server->is_enabled();

	if (enabled) {
		prompt_generation_queue_.push_back({server_name, is_system});
	} else {
		prompt_generation_queue_.insert(prompt_generation_queue_.begin(), {server_name, is_system});
	}

	if (!prompt_thread_running_) {
		prompt_thread_running_ = true;
		if (prompt_generation_thread_.joinable()) {
			prompt_generation_thread_.join();
		}
		prompt_generation_thread_ = std::thread(&mcp_manager::prompt_worker_loop, this);
	} else {
		prompt_cv_.notify_all();
	}
}

void mcp_manager::prompt_worker_loop()
{
	event_logger::get_instance().log("Thread started: mcp prompt generation worker");

	while (!project_manager::get_instance().is_exiting()) {
		std::pair<std::string, bool> task;
		{
			std::unique_lock<std::mutex> lock(prompt_mutex_);
			prompt_cv_.wait(lock, [this] {
				return project_manager::get_instance().is_exiting() || !prompt_generation_queue_.empty();
			});

			if (project_manager::get_instance().is_exiting()) {
				break;
			}
			if (prompt_generation_queue_.empty()) {
				continue;
			}

			task = prompt_generation_queue_.front();
			prompt_generation_queue_.erase(prompt_generation_queue_.begin());
		}

		try {
			std::string server_name = task.first;
			bool is_system = task.second;

			std::string cached = config_manager::get_instance().get_mcp_server_when_to_activate(server_name, is_system);
			std::string placeholder = "Activate when needing tools from the " + server_name + " family";
			if (!cached.empty() && cached != placeholder) {
				continue;
			}

			auto server = find_server(server_name);
			if (!server) {
				continue;
			}

			std::string tools_str;
			for (const auto &tool : server->get_tools()) {
				tools_str += std::format("  * {}: {}\n", tool.name, tool.description);
			}

			if (tools_str.empty()) {
				continue;
			}

			std::string system_prompt =
			    "You are a developer assistant. Your task is to generate a concise, professional \"When to Activate\" instruction phrase for a newly added Model Context Protocol (MCP) tool family (server).\n\n"
			    "Your generated phrase will be placed directly in a Markdown table that guides developer agents on when they should call `activate_tool_family`.\n\n"
			    "You must follow these formatting rules strictly:\n"
			    "1. The phrase MUST start with the exact prefix \"Activate when \" (e.g., \"Activate when working with databases, SQL queries, or SQLite databases\").\n"
			    "2. Keep it ultra-terse, professional, and clear. It must be under 100 characters in length.\n"
			    "3. Do NOT include any conversational filler, explanations, markdown formatting, or surrounding quotes. Reply ONLY with the generated phrase itself.\n\n"
			    "Examples of expected outputs:\n"
			    "- Time MCP: Activate when needing timezone conversions, current time, or calendar math.\n"
			    "- SQLite MCP: Activate when working with databases, SQL queries, or SQLite databases.\n"
			    "- Disassembler MCP: Activate when disassembling machine code or inspecting binary structures.\n"
			    "- Filesystem MCP: Activate when needing to read, write, list, or search files on the disk.\n"
			    "- Web MCP: Activate when needing to fetch web pages, read URLs, or search the web.\n"
			    "- Git MCP: Activate when checking repository status, staging files, committing, or viewing diffs.\n"
			    "- Compiler MCP: Activate when compiling source files, checking build status, or analyzing compile errors.\n";

			std::string user_prompt = std::format(
			    "Please generate the activation instruction for the following MCP server:\n\n"
			    "- Server Name: {}\n"
			    "- Provided Tools and Descriptions:\n{}",
			    server_name, tools_str);

			auto default_model = ai_model_registry::get_instance().get_default_model();
			if (!default_model) {
				// Sleep in small increments checking for exit to avoid blocking shutdown
				for (int i = 0; i < 66 && !project_manager::get_instance().is_exiting(); ++i) {
					std::this_thread::sleep_for(std::chrono::milliseconds(30));
				}
				if (project_manager::get_instance().is_exiting()) {
					break;
				}
				{
					std::lock_guard<std::mutex> lock(prompt_mutex_);
					prompt_generation_queue_.push_back(task);
				}
				continue;
			}

			std::vector<message> convo;
			message sys_msg;
			sys_msg.role = "system";
			sys_msg.content = system_prompt;
			convo.push_back(sys_msg);

			message user_msg;
			user_msg.role = "user";
			user_msg.content = user_prompt;
			convo.push_back(user_msg);

			auto transport =
			    std::make_shared<httplib_transport>(default_model->get_url(), default_model->get_api_key());
			llm_client client(transport, default_model->get_id(), default_model->get_api_type());

			if (project_manager::get_instance().is_exiting()) {
				break;
			}

			llm_chat_response res = client.send_chat(convo);
			if (project_manager::get_instance().is_exiting()) {
				break;
			}

			std::string result = res.msg.content;
			// Strip any leading/trailing quotes if the model wrapped it
			if (result.starts_with("\"") && result.ends_with("\"") && result.length() > 2) {
				result = result.substr(1, result.length() - 2);
			} else if (result.starts_with("'") && result.ends_with("'") && result.length() > 2) {
				result = result.substr(1, result.length() - 2);
			}

			// Trim leading/trailing whitespace
			result.erase(0, result.find_first_not_of(" \t\r\n"));
			result.erase(result.find_last_not_of(" \t\r\n") + 1);

			// Ensure it starts with "Activate when "
			if (!result.starts_with("Activate when ")) {
				if (result.starts_with("activate when ")) {
					result = "Activate when " + result.substr(14);
				} else {
					result = "Activate when " + result;
				}
			}

			if (!result.empty() && !result.starts_with("Error connecting") && !result.starts_with("Error parsing")) {
				config_manager::get_instance().set_mcp_server_when_to_activate(server_name, is_system, result);
				config_manager::get_instance().save_global();
				std::string project_root = project_manager::get_instance().get_project_root();
				if (!project_root.empty()) {
					config_manager::get_instance().save_project(project_root);
				}
				event_logger::get_instance().log("Generated activation description for MCP server '{}': {}", server_name, result);
			}

		} catch (const std::exception &e) {
			event_logger::get_instance().log("Error generating activation description: {}", std::string(e.what()));
		}
	}

	prompt_thread_running_ = false;
	event_logger::get_instance().log("Thread exited: mcp prompt generation worker");
}

} // namespace agentlib
