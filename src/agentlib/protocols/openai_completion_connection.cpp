#include "openai_completion_connection.h"
#include "../data/conversation.h"
#include "../../event_logger.h"
#include "../tool_registry.h"
#include "../agent_role.h"
#include "images/image_manager.h"
#include "fs_utils.h"
#include <chrono>
#include <map>
#include <algorithm>
#include <fstream>

namespace agentlib {

static nlohmann::json process_user_content_openai(const std::string &content)
{
	std::string remaining = content;
	nlohmann::json arr = nlohmann::json::array();

	while (true) {
		size_t found = remaining.find("images://");
		if (found == std::string::npos) {
			if (!remaining.empty()) {
				arr.push_back({{"type", "text"}, {"text", remaining}});
			}
			break;
		}

		if (found > 0) {
			arr.push_back({{"type", "text"}, {"text", remaining.substr(0, found)}});
		}

		size_t uri_end = found;
		while (uri_end < remaining.length() && 
		       !std::isspace(remaining[uri_end]) && 
		       remaining[uri_end] != '"' && 
		       remaining[uri_end] != '\'' && 
		       remaining[uri_end] != ')' && 
		       remaining[uri_end] != ']') {
			uri_end++;
		}

		std::string uri = remaining.substr(found, uri_end - found);
		std::string physical_path = images::image_manager::get_instance().resolve_uri(uri);
		if (!physical_path.empty()) {
			std::ifstream ifs(physical_path, std::ios::binary);
			if (ifs) {
				std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
				std::string b64 = fs_utils::base64_encode(std::span<const unsigned char>(data.data(), data.size()));
				
				images::image_metadata meta;
				std::string mime = "image/png";
				if (images::image_manager::get_instance().get_metadata(uri, meta)) {
					mime = meta.mime_type;
				}
				std::string data_url = "data:" + mime + ";base64," + b64;
				arr.push_back({{"type", "image_url"}, {"image_url", {{"url", data_url}}}});
			}
		}

		remaining = remaining.substr(uri_end);
	}

	if (arr.empty() && !content.empty()) {
		return content;
	}
	return arr;
}

openai_completion_connection::openai_completion_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
	: transport_(std::move(transport)), model_id_(std::move(model_id)), type_(type)
{
}

void openai_completion_connection::initialize() {
}

void openai_completion_connection::close() {
	if (transport_) {
		transport_->cancel();
	}
}

void openai_completion_connection::sync_history(Conversation& /*convo*/) {
}

static std::vector<message> normalize_history(const std::vector<message>& conversation) {
	std::vector<message> normalized_convo;
	std::vector<bool> consumed(conversation.size(), false);

	std::string base_system;
	size_t start_idx = 0;
	if (!conversation.empty() && conversation[0].role == "system") {
		base_system = conversation[0].content;
		start_idx = 1;
	}

	for (size_t i = start_idx; i < conversation.size(); ++i) {
		const auto& msg = conversation[i];
		if (msg.role == "tool") {
			continue;
		}

		normalized_convo.push_back(msg);

		if (msg.role == "assistant" && msg.tool_calls) {
			for (const auto& tc : *msg.tool_calls) {
				bool found = false;
				for (size_t j = i + 1; j < conversation.size(); ++j) {
					if (!consumed[j] && conversation[j].role == "tool" && conversation[j].tool_call_id && *conversation[j].tool_call_id == tc.id) {
						normalized_convo.push_back(conversation[j]);
						consumed[j] = true;
						found = true;
						break;
					}
				}

				if (!found) {
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

	if (!base_system.empty()) {
		message sys_msg;
		sys_msg.role = "system";
		sys_msg.content = base_system;
		normalized_convo.insert(normalized_convo.begin(), sys_msg);
	}

	return normalized_convo;
}

void openai_completion_connection::send_prompt(
	Conversation& convo,
	const agent_properties& properties,
	std::function<void(const stream_event&)> callback
) {
	std::vector<message> messages;
	model_capabilities caps = model_capabilities{};
	
	for (const auto& ep : convo.get_episodes()) {
		auto ep_msgs = ep->to_messages(caps);
		messages.insert(messages.end(), ep_msgs.begin(), ep_msgs.end());
	}
	if (auto curr_ep = convo.get_current_episode()) {
		auto ep_msgs = curr_ep->to_messages(caps, false);
		for (auto& m : ep_msgs) {
			m.episode_id = "";
			m.episode_level = 0;
		}
		messages.insert(messages.end(), ep_msgs.begin(), ep_msgs.end());
	}

	std::vector<message> normalized = normalize_history(messages);
	
	nlohmann::json msgs_json = nlohmann::json::array();
	for (const auto &msg : normalized) {
		nlohmann::json m_json;
		to_json(m_json, msg);
		m_json.erase("episode_id");
		m_json.erase("episode_level");
		m_json.erase("timestamp");
		m_json.erase("duration_ms");
		if (msg.role == "user" && msg.content.find("images://") != std::string::npos) {

			m_json["content"] = process_user_content_openai(msg.content);
		}
		msgs_json.push_back(m_json);
	}
	nlohmann::json payload = {{"model", model_id_}, {"messages", msgs_json}, {"stream", true}};
	payload["stream_options"] = {{"include_usage", true}};

	// add tools
	nlohmann::json tools_array = nlohmann::json::array();
	auto active_tools = tool_registry::get_instance().get_active_tools(true, properties);
	for (const auto &validator : active_tools) {
		std::string desc = validator->get_description();
		std::string tool_name = validator->get_name();
		if (tool_name.starts_with("mcp:")) {
			std::string res = tool_name;
			size_t pos = 0;
			while ((pos = res.find(':', pos)) != std::string::npos) {
				res.replace(pos, 1, "__");
				pos += 2;
			}
			tool_name = res;
		}

		nlohmann::json params = validator->get_parameters_schema();
		nlohmann::json tool_schema = {
			{"type", "function"},
			{"function", {{"name", tool_name}, {"description", desc}, {"parameters", params}}}};
		tools_array.push_back(tool_schema);
	}

	if (!tools_array.empty()) {
		payload["tools"] = tools_array;
		payload["tool_choice"] = "auto";
	}

	std::string body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::string endpoint = "/v1/chat/completions";
	if (type_ == api_type::copilot) {
		endpoint = "/chat/completions";
	}

	std::string line_buffer;
	std::string full_response_buffer;
	llm_usage final_usage{};
	std::string final_response_id = "";
	bool parsed_sse = false;

	bool success = transport_->post_stream(endpoint, body, [&](const char *data, size_t len, size_t /*off*/, size_t /*total*/) {
		full_response_buffer.append(data, len);
		line_buffer.append(data, len);

		size_t pos;
		while ((pos = line_buffer.find('\n')) != std::string::npos) {
			std::string line = line_buffer.substr(0, pos);
			line_buffer.erase(0, pos + 1);

			if (line.rfind("data: ", 0) == 0) {
				std::string json_str = line.substr(6);
				if (json_str == "[DONE]") {
					continue;
				}
				try {
					nlohmann::json chunk = nlohmann::json::parse(json_str);
					
					if (chunk.contains("id") && chunk["id"].is_string()) {
						final_response_id = chunk["id"].get<std::string>();
					}

					if (chunk.contains("usage") && !chunk["usage"].is_null()) {
						auto usage = chunk["usage"];
						if (usage.contains("prompt_tokens"))
							final_usage.prompt_tokens = usage["prompt_tokens"].get<int>();
						if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].contains("cached_tokens"))
							final_usage.cached_tokens = usage["prompt_tokens_details"]["cached_tokens"].get<int>();
						if (usage.contains("completion_tokens"))
							final_usage.completion_tokens = usage["completion_tokens"].get<int>();
						if (usage.contains("total_tokens"))
							final_usage.total_tokens = usage["total_tokens"].get<int>();
					}

					if (chunk.contains("choices") && !chunk["choices"].empty()) {
						parsed_sse = true;
						auto choice = chunk["choices"][0];
						if (choice.contains("delta")) {
							auto d = choice["delta"];
							std::string content;
							std::string reasoning_content;
							std::vector<tool_call> tool_calls;

							if (d.contains("content") && !d["content"].is_null())
								content = d["content"].get<std::string>();
							if (d.contains("reasoning_content") && !d["reasoning_content"].is_null())
								reasoning_content = d["reasoning_content"].get<std::string>();
							if (d.contains("tool_calls"))
								tool_calls = d["tool_calls"].get<std::vector<tool_call>>();

							if (!content.empty()) {
								stream_event event{ stream_event::event_type::content_chunk, content, {}, {}, final_response_id };
								callback(event);
							}
							if (!reasoning_content.empty()) {
								stream_event event{ stream_event::event_type::reasoning_chunk, reasoning_content, {}, {}, final_response_id };
								callback(event);
							}
							if (!tool_calls.empty()) {
								stream_event event{ stream_event::event_type::tool_call_delta, "", tool_calls, {}, final_response_id };
								callback(event);
							}
						}
					}
				} catch (...) {
				}
			}
		}
		return true;
	});

	if (!success && !parsed_sse) {
		std::string err = "Error: Streaming request failed to " + transport_->get_base_url();

		std::string last_err = transport_->get_last_error();
		if (!last_err.empty()) {
			err += " (" + last_err + ")";
		}
		stream_event err_event{ stream_event::event_type::error, err, {}, {}, "" };
		callback(err_event);
	} else {
		if (!parsed_sse && !full_response_buffer.empty()) {
			try {
				nlohmann::json chunk = nlohmann::json::parse(full_response_buffer);
				if (chunk.contains("error") && chunk["error"].is_object() && chunk["error"].contains("message")) {
					std::string err_msg = chunk["error"]["message"].get<std::string>();
					stream_event err_event{ stream_event::event_type::error, err_msg, {}, {}, "" };
					callback(err_event);
					return;
				}
				if (chunk.contains("id") && chunk["id"].is_string()) {
					final_response_id = chunk["id"].get<std::string>();
				}
				if (chunk.contains("usage") && !chunk["usage"].is_null()) {
					auto usage = chunk["usage"];
					if (usage.contains("prompt_tokens"))
						final_usage.prompt_tokens = usage["prompt_tokens"].get<int>();
					if (usage.contains("completion_tokens"))
						final_usage.completion_tokens = usage["completion_tokens"].get<int>();
					if (usage.contains("total_tokens"))
						final_usage.total_tokens = usage["total_tokens"].get<int>();
				}
				if (chunk.contains("choices") && !chunk["choices"].empty()) {
					auto choice = chunk["choices"][0];
					if (choice.contains("message")) {
						auto msg = choice["message"];
						std::string content;
						std::string reasoning_content;
						std::vector<tool_call> tool_calls;

						if (msg.contains("content") && !msg["content"].is_null()) {
							content = msg["content"].get<std::string>();
						}
						if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null()) {
							reasoning_content = msg["reasoning_content"].get<std::string>();
						}
						if (msg.contains("tool_calls")) {
							tool_calls = msg["tool_calls"].get<std::vector<tool_call>>();
						}
						if (!content.empty()) {
							stream_event event{ stream_event::event_type::content_chunk, content, {}, {}, final_response_id };
							callback(event);
						}
						if (!reasoning_content.empty()) {
							stream_event event{ stream_event::event_type::reasoning_chunk, reasoning_content, {}, {}, final_response_id };
							callback(event);
						}
						if (!tool_calls.empty()) {
							stream_event event{ stream_event::event_type::tool_call_delta, "", tool_calls, {}, final_response_id };
							callback(event);
						}
					}
				}
			} catch (...) {
			}
		}

		stream_event completed_event{ stream_event::event_type::completed, "", {}, final_usage, final_response_id };
		callback(completed_event);
	}
}

} // namespace agentlib
