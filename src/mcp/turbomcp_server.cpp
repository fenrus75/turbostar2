// Tested source file: src/mcp/turbomcp_server.cpp
#include "mcp/turbomcp_server.h"
#include "agentlib/ai_agent.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace agentlib
{

static editor_options make_headless_editor_options()
{
	editor_options opts;
	opts.no_welcome = true;
	opts.no_lsp = false;
	return opts;
}

turbomcp_server::turbomcp_server()
	: editor_instance_(std::make_shared<editor>(make_headless_editor_options()))
	, mcp_root_agent_(ai_agent::create(9999, "mcp_root", ai_model_registry::get_instance().get_default_model(), &editor_instance_->get_global_queue(), editor_instance_.get()))
{
	start_event_loop();
}

turbomcp_server::~turbomcp_server()
{
	stop_event_loop();
}

void turbomcp_server::start_event_loop()
{
	if (event_loop_running_.exchange(true)) {
		return;
	}
	event_loop_thread_ = std::thread(&turbomcp_server::event_loop_worker, this);
}

void turbomcp_server::stop_event_loop()
{
	if (!event_loop_running_.exchange(false)) {
		return;
	}
	if (event_loop_thread_.joinable()) {
		event_loop_thread_.join();
	}
}

void turbomcp_server::event_loop_worker()
{
	event_logger::get_instance().log("turbomcp_server: editor event loop thread started");

	while (event_loop_running_) {
		auto opt_ev = editor_instance_->get_global_queue().pop();
		if (opt_ev) {
			editor_instance_->dispatch(*opt_ev);
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		editor_instance_->update_terminal_windows();
	}

	event_logger::get_instance().log("turbomcp_server: editor event loop thread stopped");
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
	ctx.active_agent = mcp_root_agent_.get();
	ctx.properties.active_families = tool_registry::get_instance().get_all_registered_families();
	ctx.queue = &editor_instance_->get_global_queue();
	ctx.doc_provider = editor_instance_.get();

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
