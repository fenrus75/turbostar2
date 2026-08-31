#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "agentlib/tool_context.h"

namespace agentlib
{

class turbomcp_server
{
public:
	turbomcp_server() = default;
	~turbomcp_server() = default;

	// Runs the stdio JSON-RPC loop reading stdin and writing to stdout
	int run_stdio_loop();

private:
	nlohmann::json handle_request(const nlohmann::json &req);
	nlohmann::json handle_initialize(const nlohmann::json &id, const nlohmann::json &params);
	nlohmann::json handle_tools_list(const nlohmann::json &id);
	nlohmann::json handle_tools_call(const nlohmann::json &id, const nlohmann::json &params);

	void send_response(const nlohmann::json &resp);
	void send_error(const nlohmann::json &id, int code, const std::string &message);
};

} // namespace agentlib
