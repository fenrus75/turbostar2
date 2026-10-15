#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <nlohmann/json.hpp>
#include "editor.h"
#include "agentlib/tool_context.h"

namespace agentlib
{

enum class mcp_tool_identity {
	turbostar,   // Canonical snake_case parameter names (e.g. start_line)
	antigravity, // Google/Antigravity PascalCase parameter names (e.g. StartLine)
	copilot      // Microsoft/Copilot camelCase parameter names (e.g. startLine)
};

class turbomcp_server
{
public:
	turbomcp_server();
	~turbomcp_server();

	// Runs the stdio JSON-RPC loop reading stdin and writing to stdout
	int run_stdio_loop();

	// Handles a JSON-RPC request and returns the response
	nlohmann::json handle_request(const nlohmann::json &req);

	// Configures or queries the parameter naming identity advertised by tools/list
	void set_tool_identity(mcp_tool_identity identity) noexcept;
	bool set_tool_identity(std::string_view untrusted_identity_name) noexcept;
	mcp_tool_identity get_tool_identity() const noexcept { return tool_identity_; }

	// Transforms a canonical JSON Schema object's property names and required fields to match the target identity
	static nlohmann::json transform_schema_for_identity(const nlohmann::json &schema, mcp_tool_identity identity);

	// Context accessor for inspection or testing
	const tool_context &get_context() const { return mcp_context_; }

private:
	nlohmann::json handle_initialize(const nlohmann::json &id, const nlohmann::json &params);
	nlohmann::json handle_tools_list(const nlohmann::json &id);
	nlohmann::json handle_tools_call(const nlohmann::json &id, const nlohmann::json &params);

	void send_response(const nlohmann::json &resp);
	void send_error(const nlohmann::json &id, int code, const std::string &message);

	void start_event_loop();
	void stop_event_loop();
	void event_loop_worker();

	std::shared_ptr<editor> editor_instance_;
	std::shared_ptr<ai_agent> mcp_root_agent_;
	std::atomic<bool> event_loop_running_{false};
	std::thread event_loop_thread_;
	tool_context mcp_context_;
	mcp_tool_identity tool_identity_{mcp_tool_identity::turbostar};
	bool identity_explicitly_set_{false};
};

} // namespace agentlib
