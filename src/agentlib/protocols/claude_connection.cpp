#include "claude_connection.h"
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

static nlohmann::json process_user_content_claude(const std::string &content)
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
				arr.push_back({
					{"type", "image"},
					{"source", {
						{"type", "base64"},
						{"media_type", mime},
						{"data", b64}
					}}
				});
			}
		}

		remaining = remaining.substr(uri_end);
	}

	if (arr.empty() && !content.empty()) {
		return content;
	}
	return arr;
}

claude_connection::claude_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
	: transport_(std::move(transport)), model_id_(std::move(model_id)), type_(type)
{
}

void claude_connection::initialize() {
}

void claude_connection::close() {
	if (transport_) {
		transport_->cancel();
	}
}

void claude_connection::sync_history(Conversation& /*convo*/) {
}

static std::vector<message> normalize_history(const std::vector<message>& conversation) {
	std::vector<message> normalized_convo;
	std::map<std::string, message> tool_responses;

	for (const auto& msg : conversation) {
		if (msg.role == "tool" && msg.tool_call_id) {
			tool_responses[*msg.tool_call_id] = msg;
		}
	}

	for (const auto& msg : conversation) {
		if (msg.role == "tool") {
			continue;
		}

		normalized_convo.push_back(msg);

		if (msg.role == "assistant" && msg.tool_calls) {
			for (const auto& tc : *msg.tool_calls) {
				auto it = tool_responses.find(tc.id);
				if (it != tool_responses.end()) {
					normalized_convo.push_back(it->second);
					tool_responses.erase(it);
				} else {
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

void claude_connection::send_prompt(
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

	std::string system_instruction;
	nlohmann::json messages_json = nlohmann::json::array();

	for (const auto &msg : normalized) {
		if (msg.role == "system") {
			if (!system_instruction.empty()) {
				system_instruction += "\n\n";
			}
			system_instruction += msg.content;
			continue;
		}

		nlohmann::json claude_msg;
		if (msg.role == "user") {
			claude_msg["role"] = "user";
			if (msg.content.find("images://") != std::string::npos) {
				claude_msg["content"] = process_user_content_claude(msg.content);
			} else {
				claude_msg["content"] = msg.content;
			}
		} else if (msg.role == "assistant") {
			claude_msg["role"] = "assistant";
			if (!msg.tool_calls || msg.tool_calls->empty()) {
				claude_msg["content"] = msg.content;
			} else {
				nlohmann::json content_array = nlohmann::json::array();
				if (!msg.content.empty()) {
					content_array.push_back({{"type", "text"}, {"text", msg.content}});
				}
				for (const auto &tc : *msg.tool_calls) {
					nlohmann::json input_obj;
					try {
						input_obj = nlohmann::json::parse(tc.function.arguments);
					} catch (...) {
						input_obj = nlohmann::json::object();
					}
					content_array.push_back({
						{"type", "tool_use"},
						{"id", tc.id},
						{"name", tc.function.name},
						{"input", input_obj}
					});
				}
				claude_msg["content"] = content_array;
			}
		} else if (msg.role == "tool") {
			claude_msg["role"] = "user";
			nlohmann::json content_array = nlohmann::json::array();
			content_array.push_back({
				{"type", "tool_result"},
				{"tool_use_id", msg.tool_call_id ? *msg.tool_call_id : ""},
				{"content", msg.content}
			});
			claude_msg["content"] = content_array;
		}

		if (!messages_json.empty() && messages_json.back()["role"] == claude_msg["role"]) {
			auto &last_msg = messages_json.back();
			if (last_msg["content"].is_string() && claude_msg["content"].is_string()) {
				last_msg["content"] = last_msg["content"].get<std::string>() + "\n\n" + claude_msg["content"].get<std::string>();
			} else {
				nlohmann::json new_content = nlohmann::json::array();
				if (last_msg["content"].is_array()) {
					for (const auto &item : last_msg["content"]) new_content.push_back(item);
				} else {
					new_content.push_back({{"type", "text"}, {"text", last_msg["content"].get<std::string>()}});
				}
				if (claude_msg["content"].is_array()) {
					for (const auto &item : claude_msg["content"]) new_content.push_back(item);
				} else {
					new_content.push_back({{"type", "text"}, {"text", claude_msg["content"].get<std::string>()}});
				}
				last_msg["content"] = new_content;
			}
		} else {
			messages_json.push_back(claude_msg);
		}
	}

	nlohmann::json payload = {
		{"model", model_id_},
		{"messages", messages_json},
		{"max_tokens", 4096},
		{"stream", true}
	};

	if (!system_instruction.empty()) {
		payload["system"] = system_instruction;
	}

	nlohmann::json tools_array = nlohmann::json::array();
	auto active_tools = tool_registry::get_instance().get_active_tools(true, properties);
	for (const auto &validator : active_tools) {
		std::string desc = validator->get_description();
		if (validator->is_allowed_in_plan_mode_statically()) {
			if (validator->is_pure()) {
				desc += " [Read-Only: Safe for Plan Mode]";
			} else {
				desc += " [State-Modifying: Allowed in Plan Mode]";
			}
		} else {
			desc += " [State-Modifying: Blocked in Plan Mode]";
		}

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
			{"name", tool_name},
			{"description", desc},
			{"input_schema", params}
		};
		tools_array.push_back(tool_schema);
	}

	if (!tools_array.empty()) {
		payload["tools"] = tools_array;
	}

	std::string body = payload.dump();
	std::string endpoint = "/v1/messages";

	std::string line_buffer;
	llm_usage final_usage{};
	std::string final_response_id = "";
	std::map<int, tool_call> active_tool_calls;

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
					std::string type = chunk.value("type", "");

					if (type == "message_start") {
						if (chunk.contains("message") && chunk["message"].contains("id")) {
							final_response_id = chunk["message"]["id"].get<std::string>();
						}
						if (chunk.contains("message") && chunk["message"].contains("usage")) {
							auto usage = chunk["message"]["usage"];
							if (usage.contains("input_tokens"))
								final_usage.prompt_tokens = usage["input_tokens"].get<int>();
							if (usage.contains("output_tokens"))
								final_usage.completion_tokens = usage["output_tokens"].get<int>();
						}
					} else if (type == "content_block_start") {
						int index = chunk.value("index", 0);
						if (chunk.contains("content_block")) {
							auto cb = chunk["content_block"];
							std::string cb_type = cb.value("type", "");
							if (cb_type == "tool_use") {
								tool_call tc;
								tc.type = "function";
								tc.id = cb.value("id", "");
								tc.function.name = cb.value("name", "");
								tc.function.arguments = "";
								active_tool_calls[index] = tc;
							}
						}
					} else if (type == "content_block_delta") {
						int index = chunk.value("index", 0);
						if (chunk.contains("delta")) {
							auto d = chunk["delta"];
							std::string delta_type = d.value("type", "");
							if (delta_type == "text_delta") {
								std::string text = d.value("text", "");
								stream_event event{ stream_event::event_type::content_chunk, text, {}, {}, final_response_id };
								callback(event);
							} else if (delta_type == "input_json_delta") {
								std::string partial = d.value("partial_json", "");
								if (active_tool_calls.contains(index)) {
									active_tool_calls[index].function.arguments += partial;
									stream_event event{ stream_event::event_type::tool_call_delta, "", {active_tool_calls[index]}, {}, final_response_id };
									callback(event);
								}
							}
						}
					} else if (type == "message_delta") {
						if (chunk.contains("usage")) {
							auto usage = chunk["usage"];
							if (usage.contains("output_tokens"))
								final_usage.completion_tokens = usage["output_tokens"].get<int>();
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
		final_usage.total_tokens = final_usage.prompt_tokens + final_usage.completion_tokens;
		stream_event completed_event{ stream_event::event_type::completed, "", {}, final_usage, final_response_id };
		callback(completed_event);
	}
}

} // namespace agentlib
