// Tested source file: src/mcp/turbomcp_server.cpp
#include "mcp/turbomcp_server.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace agentlib
{

start_app_result headless_document_provider::start_app(std::string_view args, bool /*use_debugger*/, bool /*auto_continue*/, bool /*collect_performance*/)
{
	std::lock_guard<std::mutex> lock(mutex_);

	std::string exe = config_manager::get_instance().get_main_executable();
	if (exe.empty()) {
		event_logger::get_instance().log("headless_document_provider: start_app failed, no main executable configured");
		return {-1, -1};
	}

	std::string project_root = project_manager::get_instance().get_project_root();
	if (project_root.empty()) {
		project_root = fs_utils::get_project_dir();
	}

	std::filesystem::path full_path = std::filesystem::path(project_root) / "build" / exe;
	if (!std::filesystem::exists(full_path)) {
		full_path = std::filesystem::path(project_root) / exe;
		if (!std::filesystem::exists(full_path)) {
			full_path = exe;
		}
	}

	std::string cmd = full_path.string();
	if (!args.empty()) {
		cmd += " " + std::string(args);
	}

	int run_id = next_run_id_++;
	pid_t pid = fork();
	if (pid == 0) {
		// Child process
		execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)NULL);
		_exit(127);
	} else if (pid > 0) {
		run_info info;
		info.pid = pid;
		info.app_run_id = run_id;
		info.command = cmd;
		info.is_alive = true;
		info.start_time = std::chrono::steady_clock::now();
		runs_[run_id] = info;

		event_logger::get_instance().log("headless_document_provider: started app run_id={} pid={} cmd='{}'", run_id, pid, cmd);
		return {run_id, -1};
	}

	return {-1, -1};
}

wait_for_app_result headless_document_provider::wait_for_app(int run_id, std::string_view /*type*/, int timeout_sec)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = runs_.find(run_id);
	if (it == runs_.end()) {
		return {"not_found", 0, false, ""};
	}

	auto &info = it->second;
	auto start = std::chrono::steady_clock::now();

	while (info.is_alive) {
		int status = 0;
		pid_t res = waitpid(info.pid, &status, WNOHANG);
		if (res == info.pid) {
			info.is_alive = false;
			info.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
			break;
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
		if (elapsed >= timeout_sec) {
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	int64_t age = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - info.start_time).count();
	std::string status_str = info.is_alive ? "settled" : "ended";

	return {status_str, age, info.is_alive, ""};
}

bool headless_document_provider::terminate_run(int run_id)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = runs_.find(run_id);
	if (it == runs_.end()) {
		return false;
	}

	if (it->second.is_alive && it->second.pid > 0) {
		kill(it->second.pid, SIGTERM);
		it->second.is_alive = false;
		return true;
	}
	return false;
}

bool headless_document_provider::write_to_run(int /*run_id*/, std::string_view /*data*/)
{
	return false;
}

run_screenshot_data headless_document_provider::get_run_screenshot(int /*run_id*/)
{
	return {};
}

turbomcp_server::turbomcp_server()
	: doc_provider_(std::make_shared<headless_document_provider>())
{
}

int turbomcp_server::run_stdio_loop()
{
	event_logger::get_instance().log("turbomcp_server: starting stdio loop");

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) {
			continue;
		}

		try {
			nlohmann::json req = nlohmann::json::parse(line);
			nlohmann::json resp = handle_request(req);
			if (!resp.is_null()) {
				send_response(resp);
			}
		} catch (const std::exception &e) {
			event_logger::get_instance().log("turbomcp_server JSON parse error: {}", e.what());
			send_error(nullptr, -32700, "Parse error");
		}
	}

	event_logger::get_instance().log("turbomcp_server: stdio loop ended");
	return 0;
}

nlohmann::json turbomcp_server::handle_request(const nlohmann::json &req)
{
	if (!req.is_object()) {
		return nullptr;
	}

	std::string method = req.value("method", "");
	nlohmann::json id = req.contains("id") ? req["id"] : nullptr;
	nlohmann::json params = req.contains("params") ? req["params"] : nlohmann::json::object();

	// Notifications (no id field or id is null). In JSON-RPC 2.0, notifications MUST NOT receive a response.
	if (!req.contains("id") || req["id"].is_null()) {
		event_logger::get_instance().log("turbomcp_server received notification: {}", method);
		return nullptr;
	}

	if (method == "initialize") {
		return handle_initialize(id, params);
	}
	if (method == "ping") {
		nlohmann::json resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["result"] = nlohmann::json::object();
		return resp;
	}
	if (method == "resources/list") {
		nlohmann::json resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["result"] = {{"resources", nlohmann::json::array()}};
		return resp;
	}
	if (method == "prompts/list") {
		nlohmann::json resp;
		resp["jsonrpc"] = "2.0";
		resp["id"] = id;
		resp["result"] = {{"prompts", nlohmann::json::array()}};
		return resp;
	}
	if (method == "tools/list") {
		return handle_tools_list(id);
	}
	if (method == "tools/call") {
		return handle_tools_call(id, params);
	}

	// Unknown method
	nlohmann::json err;
	err["jsonrpc"] = "2.0";
	err["id"] = id;
	err["error"] = {{"code", -32601}, {"message", "Method not found: " + method}};
	return err;
}

nlohmann::json turbomcp_server::handle_initialize(const nlohmann::json &id, const nlohmann::json & /*params*/)
{
	nlohmann::json resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = id;

	nlohmann::json result;
	result["protocolVersion"] = "2024-11-05";
	result["capabilities"] = {
		{"tools", {{"listChanged", false}}}
	};
	result["serverInfo"] = {
		{"name", "turbomcp"},
		{"version", "0.1.0"}
	};

	resp["result"] = result;
	return resp;
}

nlohmann::json turbomcp_server::handle_tools_list(const nlohmann::json &id)
{
	nlohmann::json tools_array = nlohmann::json::array();
	auto validators = tool_registry::get_instance().get_all_registered_validators();

	for (const auto &val : validators) {
		if (!val) continue;

		// Respect expose_in_mcp() virtual method to filter out editor-only tools
		if (!val->expose_in_mcp()) {
			continue;
		}

		nlohmann::json tool_item;
		tool_item["name"] = val->get_name();
		tool_item["description"] = val->get_description();
		tool_item["inputSchema"] = val->get_parameters_schema();

		tools_array.push_back(tool_item);
	}

	nlohmann::json resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = id;
	resp["result"] = {
		{"tools", tools_array}
	};

	return resp;
}

nlohmann::json turbomcp_server::handle_tools_call(const nlohmann::json &id, const nlohmann::json &params)
{
	std::string tool_name = params.value("name", "");
	nlohmann::json args = params.contains("arguments") ? params["arguments"] : nlohmann::json::object();

	tool_context ctx;
	ctx.properties.active_families = tool_registry::get_instance().get_all_registered_families();
	ctx.queue = nullptr;
	ctx.doc_provider = doc_provider_.get();

	std::string workspace_root = project_manager::get_instance().get_project_root();
	if (workspace_root.empty()) {
		workspace_root = fs_utils::get_project_dir();
	}
	ctx.fs_security.set_working_directory(workspace_root);
	ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
	ctx.fs_security.set_vfs(skill_manager::get_instance().get_vfs());

	std::string args_str = args.dump();
	std::string res_text = tool_registry::get_instance().execute_tool(tool_name, args_str, ctx);

	bool is_error = res_text.starts_with("Error:") || res_text.starts_with("Validation Error:") || res_text.starts_with("Security Violation:");

	nlohmann::json content_item;
	content_item["type"] = "text";
	content_item["text"] = res_text;

	nlohmann::json result_obj;
	result_obj["content"] = nlohmann::json::array({content_item});
	result_obj["isError"] = is_error;

	nlohmann::json resp;
	resp["jsonrpc"] = "2.0";
	resp["id"] = id;
	resp["result"] = result_obj;

	return resp;
}

void turbomcp_server::send_response(const nlohmann::json &resp)
{
	std::cout << resp.dump() << "\n";
	std::cout.flush();
}

void turbomcp_server::send_error(const nlohmann::json &id, int code, const std::string &message)
{
	nlohmann::json err;
	err["jsonrpc"] = "2.0";
	err["id"] = id;
	err["error"] = {{"code", code}, {"message", message}};
	send_response(err);
}

} // namespace agentlib
