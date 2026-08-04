#include "openai_response_connection.h"
#include "../data/conversation.h"
#include "../../event_logger.h"
#include "../tool_registry.h"
#include "../agent_role.h"
#include "images/image_manager.h"
#include "fs_utils.h"
#include <chrono>
#include <map>
#include <algorithm>
#include <format>
#include <fstream>

namespace agentlib {

static nlohmann::json process_user_content_openai_response(const std::string &content)
{
	std::string remaining = content;
	nlohmann::json arr = nlohmann::json::array();

	while (true) {
		size_t found = remaining.find("images://");
		if (found == std::string::npos) {
			if (!remaining.empty()) {
				arr.push_back({{"type", "input_text"}, {"text", remaining}});
			}
			break;
		}

		if (found > 0) {
			arr.push_back({{"type", "input_text"}, {"text", remaining.substr(0, found)}});
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
				arr.push_back({{"type", "input_image"}, {"image_url", data_url}});
			}
		}

		remaining = remaining.substr(uri_end);
	}

	if (arr.empty() && !content.empty()) {
		arr.push_back({{"type", "input_text"}, {"text", content}});
	}
	return arr;
}

openai_response_connection::openai_response_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
	: transport_(std::move(transport)), model_id_(std::move(model_id)), type_(type)
{
}

void openai_response_connection::initialize() {
}

void openai_response_connection::close() {
	if (transport_) {
		transport_->cancel();
	}
}

void openai_response_connection::sync_history(Conversation& /*convo*/) {
}

static std::vector<message> normalize_history(const std::vector<message>& conversation) {
	std::vector<message> normalized_convo;
	std::vector<bool> consumed(conversation.size(), false);

	for (size_t i = 0; i < conversation.size(); ++i) {
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

	return normalized_convo;
}

void openai_response_connection::send_prompt(
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
	
	std::string previous_response_id = "";
	nlohmann::json state_json = convo.serialize();
	if (state_json.contains("connection_state") && state_json["connection_state"].contains("metadata")) {
		previous_response_id = state_json["connection_state"]["metadata"].value("last_response_id", "");
	}

	nlohmann::json input_json = nlohmann::json::array();
	std::string instructions_str;

	auto last_assistant_it = normalized.end();
	if (!previous_response_id.empty()) {
		for (auto it = normalized.rbegin(); it != normalized.rend(); ++it) {
			if (it->role == "assistant") {
				last_assistant_it = (it.base() - 1);
				break;
			}
		}
	}

	auto start_it = normalized.begin();
	if (last_assistant_it != normalized.end()) {
		start_it = last_assistant_it + 1;
	}

	for (auto it = start_it; it != normalized.end(); ++it) {
		const auto &msg = *it;
		if (msg.role == "system") {
			if (it == normalized.begin()) {
				instructions_str = msg.content;
			} else {
				nlohmann::json m_json = {{"type", "message"},
							   {"role", "user"},
							   {"content", process_user_content_openai_response("[System Note]\n" + msg.content)}};
				input_json.push_back(m_json);
			}
			continue;
		}

		if (msg.role == "user") {
			nlohmann::json m_json = {{"type", "message"},
						   {"role", "user"},
						   {"content", process_user_content_openai_response(msg.content)}};
			input_json.push_back(m_json);
		} else if (msg.role == "assistant") {
			if (msg.tool_calls && !msg.tool_calls->empty()) {
				if (!msg.content.empty()) {
					nlohmann::json m_json = {
						{"type", "message"},
						{"role", "assistant"},
						{"content", nlohmann::json::array({{{"type", "output_text"}, {"text", msg.content}}})}};
					input_json.push_back(m_json);
				}
				for (const auto &tc : *msg.tool_calls) {
					nlohmann::json tc_json = {{"type", "function_call"},
									{"call_id", tc.id},
									{"name", tc.function.name},
									{"arguments", tc.function.arguments},
									{"status", "completed"}};
					input_json.push_back(tc_json);
				}
			} else {
				nlohmann::json m_json = {{"type", "message"},
							   {"role", "assistant"},
							   {"content", nlohmann::json::array({{{"type", "output_text"}, {"text", msg.content}}})}};
				input_json.push_back(m_json);
			}
		} else if (msg.role == "tool") {
			nlohmann::json m_json = {{"type", "function_call_output"},
						   {"call_id", msg.tool_call_id ? *msg.tool_call_id : ""},
						   {"output", nlohmann::json::array({{{"type", "input_text"}, {"text", msg.content}}})},
						   {"status", "completed"}};
			input_json.push_back(m_json);
		}
	}

	nlohmann::json payload = {{"model", model_id_}, {"input", input_json}, {"stream", true}};

	if (!previous_response_id.empty()) {
		payload["previous_response_id"] = previous_response_id;
	}

	if (!instructions_str.empty()) {
		payload["instructions"] = instructions_str;
	}

	payload["stream_options"] = {{"include_usage", true}};

	nlohmann::json tools_array = nlohmann::json::array();
	auto active_tools = tool_registry::get_instance().get_active_tools(false, properties);
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
		if (params.contains("properties") && params["properties"].is_object()) {
			params["properties"].erase("async");
			params["properties"].erase("is_async");
		}
		if (params.contains("required") && params["required"].is_array()) {
			nlohmann::json new_req = nlohmann::json::array();
			for (const auto &item : params["required"]) {
				if (item.is_string() && (item.get<std::string>() == "async" || item.get<std::string>() == "is_async")) {
					continue;
				}
				new_req.push_back(item);
			}
			params["required"] = new_req;
		}

		nlohmann::json tool_schema = {
			{"type", "function"},
			{"name", tool_name},
			{"description", desc},
			{"parameters", params}};
		tools_array.push_back(tool_schema);
	}

	if (!tools_array.empty()) {
		payload["tools"] = tools_array;
		payload["tool_choice"] = "auto";
	}

	std::string body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::string endpoint = "/v1/responses";

	std::string line_buffer;
	llm_usage final_usage{};
	std::string final_response_id = "";

	bool success = transport_->post_stream(endpoint, body, [&](const char *data, size_t len, size_t /*off*/, size_t /*total*/) {
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
					
					if (chunk.contains("response") && chunk["response"].is_object() && chunk["response"].contains("id") &&
					    chunk["response"]["id"].is_string()) {
						final_response_id = chunk["response"]["id"].get<std::string>();
					}

					nlohmann::json usage_src;
					if (chunk.contains("response") && chunk["response"].contains("usage")) {
						usage_src = chunk["response"]["usage"];
					} else if (chunk.contains("usage")) {
						usage_src = chunk["usage"];
					}

					if (!usage_src.is_null()) {
						if (usage_src.contains("prompt_tokens"))
							final_usage.prompt_tokens = usage_src["prompt_tokens"].get<int>();
						else if (usage_src.contains("input_tokens"))
							final_usage.prompt_tokens = usage_src["input_tokens"].get<int>();

						if (usage_src.contains("prompt_tokens_details") &&
						    usage_src["prompt_tokens_details"].contains("cached_tokens"))
							final_usage.cached_tokens = usage_src["prompt_tokens_details"]["cached_tokens"].get<int>();
						else if (usage_src.contains("input_tokens_details") &&
							 usage_src["input_tokens_details"].contains("cached_tokens"))
							final_usage.cached_tokens = usage_src["input_tokens_details"]["cached_tokens"].get<int>();

						if (usage_src.contains("completion_tokens"))
							final_usage.completion_tokens = usage_src["completion_tokens"].get<int>();
						else if (usage_src.contains("output_tokens"))
							final_usage.completion_tokens = usage_src["output_tokens"].get<int>();

						if (usage_src.contains("total_tokens"))
							final_usage.total_tokens = usage_src["total_tokens"].get<int>();
					}

					if (chunk.contains("type")) {
						std::string type = chunk["type"].get<std::string>();
						if (type == "response.output_text.delta") {
							if (chunk.contains("delta") && chunk["delta"].is_string()) {
								std::string text = chunk["delta"].get<std::string>();
								stream_event event{ stream_event::event_type::content_chunk, text, {}, {}, final_response_id };
								callback(event);
							}
						} else if (type == "response.reasoning_summary_text.delta") {
							if (chunk.contains("delta") && chunk["delta"].is_string()) {
								std::string reasoning = chunk["delta"].get<std::string>();
								stream_event event{ stream_event::event_type::reasoning_chunk, reasoning, {}, {}, final_response_id };
								callback(event);
							}
						} else if (type == "response.output_item.added") {
							if (chunk.contains("item")) {
								auto item = chunk["item"];
								if (item.contains("type") && item["type"] == "function_call") {
									tool_call tc;
									tc.type = "function";
									tc.id = item.value("call_id", "");
									tc.function.name = item.value("name", "");
									tc.function.arguments = item.value("arguments", "");
									stream_event event{ stream_event::event_type::tool_call_delta, "", {tc}, {}, final_response_id };
									callback(event);
								}
							}
						} else if (type == "response.function_call_arguments.delta") {
							tool_call tc;
							tc.type = "function";
							tc.id = "";
							if (chunk.contains("delta") && chunk["delta"].is_string()) {
								tc.function.arguments = chunk["delta"].get<std::string>();
							} else if (chunk.contains("arguments") && chunk["arguments"].is_string()) {
								tc.function.arguments = chunk["arguments"].get<std::string>();
							}
							stream_event event{ stream_event::event_type::tool_call_delta, "", {tc}, {}, final_response_id };
							callback(event);
						}
					}
				} catch (...) {
				}
			}
		}
		return true;
	});

	if (!success) {
		std::string err = "Error: Streaming request failed to " + transport_->get_base_url();
		std::string last_err = transport_->get_last_error();
		if (!last_err.empty()) {
			err += " (" + last_err + ")";
		}
		stream_event err_event{ stream_event::event_type::error, err, {}, {}, "" };
		callback(err_event);
	} else {
		stream_event completed_event{ stream_event::event_type::completed, "", {}, final_usage, final_response_id };
		callback(completed_event);
	}
}

std::string openai_response_connection::compact_response(const std::string& previous_response_id, std::string* error_msg) {
	if (error_msg) {
		error_msg->clear();
	}
	if (previous_response_id.empty()) {
		if (error_msg) {
			*error_msg = "Previous response ID is empty.";
		}
		return "";
	}
	nlohmann::json body = {{"model", model_id_}, {"previous_response_id", previous_response_id}};
	std::string endpoint = "/v1/responses/compact";
	auto res = transport_->post(endpoint, body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
	if (res.status_code == 200) {
		try {
			auto json_res = nlohmann::json::parse(res.body);
			if (json_res.contains("id") && json_res["id"].is_string()) {
				return json_res["id"].get<std::string>();
			}
		} catch (...) {
			if (error_msg) {
				*error_msg = "Failed to parse compaction response.";
			}
		}
	} else {
		if (error_msg) {
			try {
				auto json_res = nlohmann::json::parse(res.body);
				if (json_res.contains("error") && json_res["error"].contains("message")) {
					*error_msg = json_res["error"]["message"].get<std::string>();
				} else {
					*error_msg = std::format("Server returned HTTP {} status.", res.status_code);
				}
			} catch (...) {
				*error_msg = std::format("Server returned HTTP {} status.", res.status_code);
			}
		}
	}
	return "";
}

} // namespace agentlib
