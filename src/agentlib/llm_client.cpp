#include "llm_client.h"
#include <iostream>

using json = nlohmann::json;

namespace agentlib
{

llm_client::llm_client(std::shared_ptr<llm_transport> transport, std::string model_id)
    : transport_(std::move(transport)), model_id_(std::move(model_id))
{
}

llm_chat_response llm_client::send_chat(const std::vector<message> &conversation, const tool_registry *registry)
{
	json payload = {{"model", model_id_}, {"messages", conversation}, {"stream", false}};

	if (registry) {
		json tools_json = registry->get_tools_json();
		if (!tools_json.empty()) {
			payload["tools"] = tools_json;
			payload["tool_choice"] = "auto";
		}
	}

	std::string body = payload.dump();

	auto res = transport_->post("/v1/chat/completions", body);

	llm_chat_response chat_response;
	chat_response.msg.role = "assistant";

	if (res.status_code == 200) {
		try {
			json response = json::parse(res.body);
			if (response.contains("model")) {
				chat_response.model = response["model"].get<std::string>();
			}
			if (response.contains("usage")) {
				auto usage = response["usage"];
				if (usage.contains("prompt_tokens"))
					chat_response.usage.prompt_tokens = usage["prompt_tokens"].get<int>();
				if (usage.contains("completion_tokens"))
					chat_response.usage.completion_tokens = usage["completion_tokens"].get<int>();
				if (usage.contains("total_tokens"))
					chat_response.usage.total_tokens = usage["total_tokens"].get<int>();
			}
			if (response.contains("choices") && !response["choices"].empty()) {
				auto msg_json = response["choices"][0]["message"];
				chat_response.msg = msg_json.get<message>();
				return chat_response;
			}
		} catch (const std::exception &e) {
			chat_response.msg.content = "Error parsing response JSON: " + std::string(e.what());
			return chat_response;
		}
	}

	std::string target_url = transport_ ? transport_->get_base_url() : "unknown";
	chat_response.msg.content = "Error connecting to LLM server at " + target_url + ". Status: " + std::to_string(res.status_code) +
				    "\nResponse body: " + res.body;
	return chat_response;
}

void llm_client::send_chat_stream(const std::vector<message> &conversation, std::function<void(const chat_delta &)> callback,
				  const tool_registry *registry)
{
	json payload = {
	    {"model", model_id_}, {"messages", conversation}, {"stream", true}, {"stream_options", {{"include_usage", true}}}};

	if (registry) {
		json tools_json = registry->get_tools_json();
		if (!tools_json.empty()) {
			payload["tools"] = tools_json;
			payload["tool_choice"] = "auto";
		}
	}

	std::string body = payload.dump();
	std::string line_buffer;

	bool success =
	    transport_->post_stream("/v1/chat/completions", body, [&](const char *data, size_t len, size_t /*off*/, size_t /*total*/) {
		    line_buffer.append(data, len);

		    size_t pos;
		    while ((pos = line_buffer.find('\n')) != std::string::npos) {
			    std::string line = line_buffer.substr(0, pos);
			    line_buffer.erase(0, pos + 1);

			    if (line.rfind("data: ", 0) == 0) {
				    std::string json_str = line.substr(6);
				    if (json_str == "[DONE]") {
					    chat_delta delta;
					    delta.is_final = true;
					    callback(delta);
					    return true;
				    }

				    try {
					    json chunk = json::parse(json_str);
					    chat_delta delta;

					    if (chunk.contains("usage") && !chunk["usage"].is_null()) {
						    auto usage = chunk["usage"];
						    if (usage.contains("prompt_tokens"))
							    delta.usage.prompt_tokens = usage["prompt_tokens"].get<int>();
						    if (usage.contains("completion_tokens"))
							    delta.usage.completion_tokens = usage["completion_tokens"].get<int>();
						    if (usage.contains("total_tokens"))
							    delta.usage.total_tokens = usage["total_tokens"].get<int>();
					    }

					    if (chunk.contains("choices") && !chunk["choices"].empty()) {
						    auto choice = chunk["choices"][0];
						    if (choice.contains("delta")) {
							    auto d = choice["delta"];
							    if (d.contains("role"))
								    delta.role = d["role"].get<std::string>();
							    if (d.contains("content") && !d["content"].is_null())
								    delta.content = d["content"].get<std::string>();
							    if (d.contains("reasoning_content") && !d["reasoning_content"].is_null()) {
								    delta.reasoning_content = d["reasoning_content"].get<std::string>();
							    }
							    if (d.contains("tool_calls")) {
								    delta.tool_calls = d["tool_calls"].get<std::vector<tool_call>>();
							    }
						    }
						    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
							    delta.is_final = true;
						    }
					    }
					    callback(delta);
				    } catch (...) {
					    // Ignore malformed chunks
				    }
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

} // namespace agentlib
