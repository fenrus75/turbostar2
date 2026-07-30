#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace a2a
{

struct a2a_agent_card {
	std::string name;
	std::string description;
	std::string url;
	std::vector<std::string> skills;
	nlohmann::json raw_card;
};

struct a2a_task_result {
	bool success = false;
	std::string task_id;
	std::string status; // "enqueued", "running", "completed", "failed", "cancelled"
	std::string output_text;
	std::string error_message;
	int progress_percent = 0;
	nlohmann::json raw_response;
};

class a2a_client
{
      public:
	static a2a_client &get_instance();

	/*
	 * Fetches the agent card from an A2A server.
	 * Tries GET /.well-known/agent-card.json, falling back to GET /agent.json.
	 */
	std::optional<a2a_agent_card> fetch_agent_card(std::string_view server_url, std::string &out_error, std::string_view auth_token = "");
	std::vector<a2a_agent_card> fetch_all_cards(std::string_view server_url, std::string &out_error, std::string_view auth_token = "");

	/*
	 * Submits a new task to a remote agent on server_url.
	 * Sends POST /a2a/v1/agents/{agent_name}/tasks with input payload.
	 */
	a2a_task_result submit_task(std::string_view server_url, std::string_view agent_name, std::string_view prompt, std::string_view auth_token = "", std::string_view repository_url = "", std::string_view git_ref = "");

	/*
	 * Queries the current status and output of task_id on server_url.
	 * Sends GET /a2a/v1/tasks/{task_id}.
	 */
	a2a_task_result poll_task(std::string_view server_url, std::string_view task_id, std::string_view auth_token = "");

	/*
	 * Synchronously executes a task on server_url: submits the task and polls until completion or timeout.
	 */
	a2a_task_result execute_task_sync(std::string_view server_url, std::string_view agent_name, std::string_view prompt, int timeout_seconds = 60, std::string_view auth_token = "", std::string_view repository_url = "", std::string_view git_ref = "");

	/*
	 * Cancels a running or enqueued task on server_url.
	 * Sends DELETE /a2a/v1/tasks/{task_id}.
	 */
	bool cancel_task(std::string_view server_url, std::string_view task_id, std::string_view auth_token = "");

      private:
	a2a_client() = default;
	~a2a_client() = default;
};

} // namespace a2a
