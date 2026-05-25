#include "llm_client.h"
#include <iostream>

using json = nlohmann::json;

namespace agentlib
{

llm_client::llm_client(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
    : transport_(std::move(transport)), model_id_(std::move(model_id))
{
	formatter_ = api_formatter::create(type);
}

llm_chat_response llm_client::send_chat(const std::vector<message> &conversation, const tool_registry *registry)
{
	std::string body = formatter_->build_chat_payload(model_id_, conversation, registry, false);

	auto res = transport_->post("/v1/chat/completions", body);

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
				  const tool_registry *registry)
{
	std::string body = formatter_->build_chat_payload(model_id_, conversation, registry, true);
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
				    chat_delta delta = formatter_->parse_stream_chunk(json_str);
				    if (!delta.content.empty() || !delta.reasoning_content.empty() || delta.tool_calls || delta.is_final) {
					    callback(delta);
				    }
				    if (delta.is_final) return true;
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
