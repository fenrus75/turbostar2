#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>
#include "editor.h"

namespace agentlib
{

class turbomcp_server
{
public:
	turbomcp_server();
	~turbomcp_server();

	// Runs the stdio JSON-RPC loop reading stdin and writing to stdout
	int run_stdio_loop();

private:
	nlohmann::json handle_request(const nlohmann::json &req);
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
};

} // namespace agentlib
