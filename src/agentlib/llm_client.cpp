#include "llm_client.h"
#include <iostream>
#include <map>

using json = nlohmann::json;

namespace agentlib
{

llm_client::llm_client(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
    : transport_(std::move(transport)), model_id_(std::move(model_id))
{
	formatter_ = api_formatter::create(type);
}

static std::vector<message> normalize_conversation_history(const std::vector<message> &conversation)
{
	std::vector<message> normalized_convo;
	std::map<std::string, message> tool_responses;

	// 1. Extract all tool responses
	for (const auto &msg : conversation) {
		if (msg.role == "tool" && msg.tool_call_id) {
			tool_responses[*msg.tool_call_id] = msg;
		}
	}

	// 2. Reconstruct the conversation in the correct order
	for (const auto &msg : conversation) {
		if (msg.role == "tool") {
			// Skip tool messages; they will be inserted right after their corresponding assistant messages
			continue;
		}

		normalized_convo.push_back(msg);

		if (msg.role == "assistant" && msg.tool_calls) {
			for (const auto &tc : *msg.tool_calls) {
				auto it = tool_responses.find(tc.id);
				if (it != tool_responses.end()) {
					normalized_convo.push_back(it->second);
					tool_responses.erase(it);
				} else {
					// Pending tool call with no response: Create a placeholder response to keep sequence valid
					message abort_msg;
					abort_msg.role = "tool";
					abort_msg.tool_call_id = tc.id;
					abort_msg.name = tc.function.name;
					abort_msg.content = "Tool execution status: pending/aborted.";
					normalized_convo.push_back(abort_msg);
				}
			}
		}
	}

	return normalized_convo;
}

llm_chat_response llm_client::send_chat(const std::vector<message> &conversation, const tool_registry *registry,
					const std::vector<std::string> &active_families, const std::string &previous_response_id)
{
	std::vector<message> normalized = normalize_conversation_history(conversation);
	std::string body = formatter_->build_chat_payload(model_id_, normalized, registry, false, active_families, previous_response_id);
	std::string endpoint = formatter_->get_endpoint_path(model_id_, false);

	auto res = transport_->post(endpoint, body);

	if (res.status_code == 200) {
		return formatter_->parse_sync_response(res.body);
	}

	llm_chat_response chat_response;
	chat_response.msg.role = "assistant";
	std::string target_url = transport_ ? transport_->get_base_url() : "unknown";
	chat_response.msg.content = "Error connecting to LLM server at " + target_url + ". Status: " + std::to_string(res.status_code) +
				    "\nResponse body: " + res.body;
	return chat_response;
}

void llm_client::send_chat_stream(const std::vector<message> &conversation, std::function<void(const chat_delta &)> callback,
				  const tool_registry *registry, const std::vector<std::string> &active_families,
				  const std::string &previous_response_id)
{
	std::vector<message> normalized = normalize_conversation_history(conversation);
	std::string body = formatter_->build_chat_payload(model_id_, normalized, registry, true, active_families, previous_response_id);
	std::string endpoint = formatter_->get_endpoint_path(model_id_, true);
	std::string line_buffer;

	bool success = transport_->post_stream(endpoint, body, [&](const char *data, size_t len, size_t /*off*/, size_t /*total*/) {
		line_buffer.append(data, len);

		size_t pos;
		while ((pos = line_buffer.find('\n')) != std::string::npos) {
			std::string line = line_buffer.substr(0, pos);
			line_buffer.erase(0, pos + 1);

			if (line.rfind("data: ", 0) == 0) {
				std::string json_str = line.substr(6);
				chat_delta delta = formatter_->parse_stream_chunk(json_str);
				if (!delta.content.empty() || !delta.reasoning_content.empty() || delta.tool_calls || delta.is_final ||
				    delta.usage.total_tokens > 0 || !delta.response_id.empty()) {
					callback(delta);
				}
				if (delta.is_final)
					return true;
			}
		}
		return true;
	});

	if (!success) {
		chat_delta error_delta;
		error_delta.content = "Error: Streaming request failed to " + transport_->get_base_url();
		std::string last_err = transport_->get_last_error();
		if (!last_err.empty()) {
			error_delta.content += " (" + last_err + ")";
		}
		error_delta.is_final = true;
		callback(error_delta);
	}
}

void llm_client::cancel()
{
	if (transport_) {
		transport_->cancel();
	}
}

std::string llm_client::compact_response(const std::string &previous_response_id)
{
	if (previous_response_id.empty()) {
		return "";
	}
	nlohmann::json body = {{"model", model_id_}, {"previous_response_id", previous_response_id}};
	std::string endpoint = "/v1/responses/compact";
	auto res = transport_->post(endpoint, body.dump());
	if (res.status_code == 200) {
		try {
			auto json_res = nlohmann::json::parse(res.body);
			if (json_res.contains("id") && json_res["id"].is_string()) {
				return json_res["id"].get<std::string>();
			}
		} catch (...) {
			// ignore
		}
	}
	return "";
}

} // namespace agentlib
