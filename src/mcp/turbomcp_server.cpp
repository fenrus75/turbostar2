// Tested source file: src/mcp/turbomcp_server.cpp
#include "mcp/turbomcp_server.h"
#include "agentlib/ai_agent.h"
#include "agentlib/llm_types.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "utf8.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include "pcre2_regex.h"
#include <string>
#include <thread>
#include <vector>

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
	mcp_context_.active_agent = mcp_root_agent_.get();
	mcp_context_.properties.active_families = tool_registry::get_instance().get_all_registered_families();
	mcp_context_.queue = &editor_instance_->get_global_queue();
	mcp_context_.doc_provider = editor_instance_.get();

	std::string workspace_root = project_manager::get_instance().get_project_root();
	if (workspace_root.empty()) {
		workspace_root = fs_utils::get_project_dir();
	}
	if (workspace_root.empty() || !std::filesystem::is_directory(workspace_root)) {
		workspace_root = std::filesystem::current_path().string();
	}
	mcp_context_.fs_security.set_working_directory(workspace_root);
	mcp_context_.fs_security.add_allowed_root(workspace_root, access_type::write);
	mcp_context_.fs_security.set_vfs(skill_manager::get_instance().get_vfs());

	const char *untrusted_env_identity = std::getenv("TURBOSTAR_MCP_IDENTITY");
	if (untrusted_env_identity != nullptr && *untrusted_env_identity != '\0') {
		set_tool_identity(untrusted_env_identity);
	}

	start_event_loop();
}

static void sync_utf8_param_case_style(mcp_tool_identity identity) noexcept
{
	switch (identity) {
	case mcp_tool_identity::antigravity:
		utf8::set_active_param_case_style(utf8::param_case_style::pascal_case);
		break;
	case mcp_tool_identity::copilot:
		utf8::set_active_param_case_style(utf8::param_case_style::camel_case);
		break;
	case mcp_tool_identity::turbostar:
	default:
		utf8::set_active_param_case_style(utf8::param_case_style::snake_case);
		break;
	}
}

void turbomcp_server::set_tool_identity(mcp_tool_identity identity) noexcept
{
	tool_identity_ = identity;
	identity_explicitly_set_ = true;
	sync_utf8_param_case_style(identity);
}

bool turbomcp_server::set_tool_identity(std::string_view untrusted_identity_name) noexcept
{
	std::string clean_id = utf8::to_snake_case(untrusted_identity_name);
	if (clean_id == "turbostar" || clean_id == "snake_case" || clean_id == "default") {
		set_tool_identity(mcp_tool_identity::turbostar);
		return true;
	}
	if (clean_id == "antigravity" || clean_id == "agy" || clean_id == "pascal_case") {
		set_tool_identity(mcp_tool_identity::antigravity);
		return true;
	}
	if (clean_id == "copilot" || clean_id == "vscode" || clean_id == "github_copilot" || clean_id == "camel_case") {
		set_tool_identity(mcp_tool_identity::copilot);
		return true;
	}
	return false;
}

std::string turbomcp_server::transform_description_for_identity(
	std::string_view description,
	std::span<const std::pair<std::string, std::string>> renamed_params)
{
	if (description.empty() || renamed_params.empty()) {
		return std::string(description);
	}

	std::string result(description);

	// Build an alternation of all known old/new parameter names in this tool for slash-separated lists
	// such as "start/end/length".
	std::string param_alt;
	for (const auto &[old_key, new_key] : renamed_params) {
		if (old_key.empty() || old_key == new_key) {
			continue;
		}
		if (!param_alt.empty()) {
			param_alt += '|';
		}
		param_alt += pcre2_regex::quote_meta(old_key) + "|" + pcre2_regex::quote_meta(new_key);
	}

	for (const auto &[old_key, new_key] : renamed_params) {
		if (old_key.empty() || old_key == new_key) {
			continue;
		}
		std::string escaped_old = pcre2_regex::quote_meta(old_key);
		if (old_key.find('_') != std::string::npos) {
			// Multi-word snake_case parameter names (e.g. start_line) are unambiguous identifiers
			result = pcre2_regex("\\b" + escaped_old + "\\b").global_replace(result, new_key);
		} else {
			// Single-word parameter names (e.g. tail, length, start, end): replace when quoted/backticked
			// or when part of a slash-delimited parameter list (e.g. start/end/length).
			result = pcre2_regex("(['\"`])" + escaped_old + "(['\"`])").global_replace(result, "\\1" + new_key + "\\2");
			if (!param_alt.empty()) {
				result = pcre2_regex("\\b" + escaped_old + "/(" + param_alt + ")\\b").global_replace(result, new_key + "/\\1");
				result = pcre2_regex("\\b(" + param_alt + ")/" + escaped_old + "\\b").global_replace(result, "\\1/" + new_key);
			}
		}
	}

	return result;
}

nlohmann::json turbomcp_server::transform_schema_for_identity(const nlohmann::json &schema, mcp_tool_identity identity,
							      std::string *in_out_tool_description)
{
	if (identity == mcp_tool_identity::turbostar || !schema.is_object()) {
		return schema;
	}

	auto format_key = [identity](std::string_view key) -> std::string {
		if (identity == mcp_tool_identity::antigravity) {
			return utf8::to_PascalCase(key);
		}
		if (identity == mcp_tool_identity::copilot) {
			return utf8::to_camelCase(key);
		}
		return std::string(key);
	};

	std::vector<std::pair<std::string, std::string>> renamed_params;
	auto record_rename = [&renamed_params](const std::string &old_key, const std::string &new_key) {
		if (old_key == new_key) {
			return;
		}
		for (const auto &[existing_old, _] : renamed_params) {
			if (existing_old == old_key) {
				return;
			}
		}
		renamed_params.emplace_back(old_key, new_key);
	};

	auto rename_keys_recursive = [&](auto &self, const nlohmann::json &node) -> nlohmann::json {
		if (!node.is_object()) {
			return node;
		}
		nlohmann::json out = node;
		if (out.contains("properties") && out["properties"].is_object()) {
			nlohmann::json new_props = nlohmann::json::object();
			for (auto it = out["properties"].begin(); it != out["properties"].end(); ++it) {
				nlohmann::json prop_schema = it.value();
				if (prop_schema.is_object()) {
					if (prop_schema.contains("properties")) {
						prop_schema = self(self, prop_schema);
					}
					if (prop_schema.contains("items") && prop_schema["items"].is_object()) {
						prop_schema["items"] = self(self, prop_schema["items"]);
					}
				}
				std::string mapped_key = format_key(it.key());
				record_rename(it.key(), mapped_key);
				new_props[mapped_key] = std::move(prop_schema);
			}
			out["properties"] = std::move(new_props);
		}

		if (out.contains("required") && out["required"].is_array()) {
			nlohmann::json new_req = nlohmann::json::array();
			for (const auto &req_item : out["required"]) {
				if (req_item.is_string()) {
					std::string old_req = req_item.get<std::string>();
					std::string mapped_req = format_key(old_req);
					record_rename(old_req, mapped_req);
					new_req.push_back(std::move(mapped_req));
				} else {
					new_req.push_back(req_item);
				}
			}
			out["required"] = std::move(new_req);
		}
		return out;
	};

	nlohmann::json out = rename_keys_recursive(rename_keys_recursive, schema);

	auto rewrite_descriptions_recursive = [&](auto &self, nlohmann::json &node) -> void {
		if (!node.is_object()) {
			return;
		}
		if (node.contains("description") && node["description"].is_string()) {
			node["description"] = transform_description_for_identity(
				node["description"].get<std::string>(), renamed_params);
		}
		if (node.contains("properties") && node["properties"].is_object()) {
			for (auto it = node["properties"].begin(); it != node["properties"].end(); ++it) {
				self(self, it.value());
			}
		}
		if (node.contains("items") && node["items"].is_object()) {
			self(self, node["items"]);
		}
	};

	rewrite_descriptions_recursive(rewrite_descriptions_recursive, out);

	if (in_out_tool_description != nullptr && !in_out_tool_description->empty()) {
		*in_out_tool_description = transform_description_for_identity(*in_out_tool_description, renamed_params);
	}

	return out;
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
	fs_utils::set_current_thread_name("mcp_evloop");
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
			event_logger::get_instance().log_error("turbomcp_server JSON parse error: {}", e.what());
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

nlohmann::json turbomcp_server::handle_initialize(const nlohmann::json &id, const nlohmann::json &untrusted_params)
{
	if (!identity_explicitly_set_ && untrusted_params.is_object() && untrusted_params.contains("clientInfo") &&
	    untrusted_params["clientInfo"].is_object()) {
		std::string untrusted_client_name = untrusted_params["clientInfo"].value("name", "");
		std::string clean_client = utf8::to_snake_case(untrusted_client_name);
		if (clean_client.find("antigravity") != std::string::npos || clean_client == "agy") {
			tool_identity_ = mcp_tool_identity::antigravity;
			sync_utf8_param_case_style(tool_identity_);
			event_logger::get_instance().log("turbomcp_server: auto-detected 'antigravity' tool identity from client '{}'",
							 clean_client);
		} else if (clean_client.find("copilot") != std::string::npos || clean_client.find("vscode") != std::string::npos ||
			   clean_client.find("visual_studio") != std::string::npos) {
			tool_identity_ = mcp_tool_identity::copilot;
			sync_utf8_param_case_style(tool_identity_);
			event_logger::get_instance().log("turbomcp_server: auto-detected 'copilot' tool identity from client '{}'",
							 clean_client);
		}
	}

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
		std::string tool_desc = val->get_description();
		tool_item["inputSchema"] =
			transform_schema_for_identity(val->get_parameters_schema(), tool_identity_, &tool_desc);
		tool_item["description"] = std::move(tool_desc);

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

	// Normalize tool call arguments (handles aliases like query -> pattern, search_path -> path, etc.)
	tool_call tc;
	tc.function.name = tool_name;
	tc.function.arguments = args.dump();
	normalize_tool_call(tc);
	tool_name = tc.function.name;
	try {
		args = nlohmann::json::parse(tc.function.arguments);
	} catch (...) {
	}

	// Reject execution of tools not exposed over the MCP interface
	for (const auto &val : tool_registry::get_instance().get_all_registered_validators()) {
		if (val && val->get_name() == tool_name) {
			if (!val->expose_in_mcp()) {
				nlohmann::json content_item;
				content_item["type"] = "text";
				content_item["text"] = "Error: Tool '" + tool_name + "' is not available via MCP.";
				nlohmann::json result_obj;
				result_obj["content"] = nlohmann::json::array({content_item});
				result_obj["isError"] = true;
				nlohmann::json resp;
				resp["jsonrpc"] = "2.0";
				resp["id"] = id;
				resp["result"] = result_obj;
				return resp;
			}
			break;
		}
	}

	// Ensure families and security working directory are current
	mcp_context_.properties.active_families = tool_registry::get_instance().get_all_registered_families();
	std::string workspace_root = project_manager::get_instance().get_project_root();
	if (workspace_root.empty()) {
		workspace_root = fs_utils::get_project_dir();
	}
	if (workspace_root.empty() || !std::filesystem::is_directory(workspace_root)) {
		workspace_root = std::filesystem::current_path().string();
	}
	mcp_context_.fs_security.set_working_directory(workspace_root);
	mcp_context_.fs_security.add_allowed_root(workspace_root, access_type::write);
	mcp_context_.fs_security.set_vfs(skill_manager::get_instance().get_vfs());

	std::string args_str = args.dump();
	std::string res_text = tool_registry::get_instance().execute_tool(tool_name, args_str, mcp_context_);

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
