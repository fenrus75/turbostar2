#pragma once

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include "agentlib/document_provider.h"
#include "agentlib/tool_context.h"

namespace agentlib
{

class headless_document_provider : public document_provider
{
public:
	headless_document_provider() = default;
	~headless_document_provider() override = default;

	std::vector<std::string> get_open_document_paths() const override {
		return {};
	}
	std::unique_ptr<document_snapshot> get_open_document(std::string_view /*safe_path*/) const override {
		return nullptr;
	}
	bool apply_live_edits(std::string_view /*safe_path*/, std::string_view /*edits_json_payload*/) override {
		return false;
	}
	void save_all_documents() override {}

	start_app_result start_app(std::string_view args, bool use_debugger, bool auto_continue = true, bool collect_performance = false) override;
	wait_for_app_result wait_for_app(int run_id, std::string_view type, int timeout_sec) override;
	bool terminate_run(int run_id) override;
	bool write_to_run(int run_id, std::string_view data) override;
	run_screenshot_data get_run_screenshot(int run_id) override;

private:
	struct run_info {
		pid_t pid = -1;
		int app_run_id = -1;
		std::string command;
		bool is_alive = false;
		int exit_code = 0;
		std::chrono::steady_clock::time_point start_time;
	};

	mutable std::mutex mutex_;
	int next_run_id_ = 1;
	std::map<int, run_info> runs_;
};

class turbomcp_server
{
public:
	turbomcp_server();
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

	std::shared_ptr<headless_document_provider> doc_provider_;
};

} // namespace agentlib
