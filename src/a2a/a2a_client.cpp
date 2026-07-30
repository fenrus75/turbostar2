#include "a2a/a2a_client.h"
#include <chrono>
#include <format>
#include <regex>
#include <thread>
#include <httplib.h>
#include "event_logger.h"

namespace a2a
{

a2a_client &a2a_client::get_instance()
{
	static a2a_client instance;
	return instance;
}

static httplib::Headers make_headers(std::string_view auth_token)
{
	httplib::Headers headers;
	headers.emplace("Content-Type", "application/json");
	if (!auth_token.empty()) {
		headers.emplace("Authorization", std::format("Bearer {}", auth_token));
	}
	return headers;
}

std::optional<a2a_agent_card> a2a_client::fetch_agent_card(std::string_view server_url, std::string &out_error, std::string_view auth_token)
{
	std::string url_str(server_url);
	httplib::Client cli(url_str);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(10);

	auto headers = make_headers(auth_token);

	// Attempt 1: /.well-known/agent-card.json
	auto res = cli.Get("/.well-known/agent-card.json", headers);
	if (!res || res->status != 200) {
		// Attempt 2: /agent.json
		res = cli.Get("/agent.json", headers);
	}

	if (!res) {
		out_error = std::format("Failed to connect to A2A server at '{}'", server_url);
		return std::nullopt;
	}

	if (res->status != 200) {
		out_error = std::format("A2A server returned HTTP status {}", res->status);
		return std::nullopt;
	}

	try {
		nlohmann::json j = nlohmann::json::parse(res->body);
		a2a_agent_card card;
		card.name = j.value("name", "");
		card.description = j.value("description", "");
		card.url = url_str;
		card.raw_card = j;

		if (j.contains("skills") && j["skills"].is_array()) {
			for (const auto &sk : j["skills"]) {
				if (sk.is_string()) {
					card.skills.push_back(sk.get<std::string>());
				} else if (sk.is_object() && sk.contains("name")) {
					card.skills.push_back(sk["name"].get<std::string>());
				}
			}
		}

		return card;
	} catch (const std::exception &e) {
		out_error = std::format("Failed to parse agent card JSON: {}", e.what());
		return std::nullopt;
	}
}

a2a_task_result a2a_client::submit_task(std::string_view server_url, std::string_view agent_name, std::string_view prompt, std::string_view auth_token)
{
	a2a_task_result res_info;
	std::string url_str(server_url);
	httplib::Client cli(url_str);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(30);

	nlohmann::json req_body;
	req_body["input_params"] = {{"prompt", std::string(prompt)}};

	std::string endpoint = std::format("/a2a/v1/agents/{}/tasks", agent_name);
	auto headers = make_headers(auth_token);

	auto res = cli.Post(endpoint, headers, req_body.dump(), "application/json");
	if (!res) {
		res_info.error_message = std::format("Failed to connect to A2A server at '{}'", server_url);
		return res_info;
	}

	if (res->status != 200 && res->status != 201 && res->status != 202) {
		res_info.error_message = std::format("A2A server task submit returned HTTP {}", res->status);
		return res_info;
	}

	try {
		nlohmann::json j = nlohmann::json::parse(res->body);
		res_info.raw_response = j;

		if (j.contains("task_id")) {
			res_info.task_id = j.value("task_id", "");
		} else if (j.contains("id")) {
			res_info.task_id = j.value("id", "");
		}

		res_info.status = j.value("status", "enqueued");
		res_info.success = !res_info.task_id.empty();
	} catch (const std::exception &e) {
		res_info.error_message = std::format("Failed to parse task creation response: {}", e.what());
	}

	return res_info;
}

a2a_task_result a2a_client::poll_task(std::string_view server_url, std::string_view task_id, std::string_view auth_token)
{
	a2a_task_result res_info;
	res_info.task_id = std::string(task_id);

	std::string url_str(server_url);
	httplib::Client cli(url_str);
	cli.set_connection_timeout(5);
	cli.set_read_timeout(15);

	std::string endpoint = std::format("/a2a/v1/tasks/{}", task_id);
	auto headers = make_headers(auth_token);

	auto res = cli.Get(endpoint, headers);
	if (!res) {
		res_info.error_message = std::format("Failed to connect to A2A server at '{}'", server_url);
		return res_info;
	}

	if (res->status != 200) {
		res_info.error_message = std::format("A2A server task status returned HTTP {}", res->status);
		return res_info;
	}

	try {
		nlohmann::json j = nlohmann::json::parse(res->body);
		res_info.raw_response = j;
		res_info.status = j.value("status", "unknown");

		if (j.contains("output_result")) {
			const auto &out = j["output_result"];
			if (out.is_string()) {
				res_info.output_text = out.get<std::string>();
			} else if (out.is_object() && out.contains("text")) {
				res_info.output_text = out["text"].get<std::string>();
			} else if (out.is_object() && out.contains("result")) {
				res_info.output_text = out["result"].get<std::string>();
			} else {
				res_info.output_text = out.dump();
			}
		}

		if (j.contains("error_message")) {
			res_info.error_message = j.value("error_message", "");
		}

		if (j.contains("progress_percent")) {
			res_info.progress_percent = j.value("progress_percent", 0);
		}

		if (res_info.status == "completed") {
			res_info.success = true;
		}
	} catch (const std::exception &e) {
		res_info.error_message = std::format("Failed to parse task status response: {}", e.what());
	}

	return res_info;
}

a2a_task_result a2a_client::execute_task_sync(std::string_view server_url, std::string_view agent_name, std::string_view prompt, int timeout_seconds, std::string_view auth_token)
{
	auto res = submit_task(server_url, agent_name, prompt, auth_token);
	if (!res.success || res.task_id.empty()) {
		return res;
	}

	auto start_time = std::chrono::steady_clock::now();
	while (true) {
		auto status_res = poll_task(server_url, res.task_id, auth_token);
		if (status_res.status == "completed") {
			return status_res;
		}
		if (status_res.status == "failed" || status_res.status == "cancelled") {
			status_res.success = false;
			return status_res;
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count();
		if (elapsed >= timeout_seconds) {
			status_res.success = false;
			status_res.error_message = std::format("Timed out after {} seconds waiting for task {}", timeout_seconds, res.task_id);
			return status_res;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

bool a2a_client::cancel_task(std::string_view server_url, std::string_view task_id, std::string_view auth_token)
{
	std::string url_str(server_url);
	httplib::Client cli(url_str);
	cli.set_connection_timeout(5);

	std::string endpoint = std::format("/a2a/v1/tasks/{}", task_id);
	auto headers = make_headers(auth_token);

	auto res = cli.Delete(endpoint, headers);
	return res && (res->status == 200 || res->status == 204);
}

} // namespace a2a
