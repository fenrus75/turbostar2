#include "gemini_connection.h"
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

static void clean_gemini_schema(nlohmann::json &val)
{
	if (val.is_object()) {
		val.erase("additionalProperties");
		for (auto it = val.begin(); it != val.end(); ++it) {
			clean_gemini_schema(*it);
		}
	} else if (val.is_array()) {
		for (auto &item : val) {
			clean_gemini_schema(item);
		}
	}
}

static nlohmann::json process_user_parts_gemini(const std::string &content)
{
	std::string remaining = content;
	nlohmann::json arr = nlohmann::json::array();

	while (true) {
		size_t found = remaining.find("images://");
		if (found == std::string::npos) {
			if (!remaining.empty()) {
				arr.push_back({{"text", remaining}});
			}
			break;
		}

		if (found > 0) {
			arr.push_back({{"text", remaining.substr(0, found)}});
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
					{"inlineData", {
						{"mimeType", mime},
						{"data", b64}
					}}
				});
			}
		}

		remaining = remaining.substr(uri_end);
	}

	if (arr.empty() && !content.empty()) {
		arr.push_back({{"text", content}});
	}
	return arr;
}

gemini_connection::gemini_connection(std::shared_ptr<llm_transport> transport, std::string model_id, api_type type)
	: transport_(std::move(transport)), model_id_(std::move(model_id)), type_(type)
{
}

void gemini_connection::initialize() {
}

void gemini_connection::close() {
	if (transport_) {
		transport_->cancel();
	}
}

void gemini_connection::sync_history(Conversation& /*convo*/) {
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

void gemini_connection::send_prompt(
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

	nlohmann::json payload = nlohmann::json::object();
	nlohmann::json contents = nlohmann::json::array();
	std::string system_instruction;

	for (const auto &msg : normalized) {
		if (msg.role == "system") {
			if (!system_instruction.empty()) {
				system_instruction += "\n\n";
			}
			system_instruction += msg.content;
			continue;
		}

		nlohmann::json parts_array = nlohmann::json::array();

		if (msg.role == "tool") {
			nlohmann::json function_response = nlohmann::json::object();
			function_response["name"] = msg.name ? *msg.name : "";
			try {
				function_response["response"] = {{"result", nlohmann::json::parse(msg.content)}};
			} catch (...) {
				function_response["response"] = {{"result", msg.content}};
			}
			parts_array.push_back({{"functionResponse", function_response}});
		} else if (msg.role == "assistant") {
			if (msg.tool_calls && !msg.tool_calls->empty()) {
				if (!msg.content.empty()) {
					parts_array.push_back({{"text", msg.content}});
				}
				for (const auto &tc : *msg.tool_calls) {
					nlohmann::json func_call = {{"name", tc.function.name}};
					try {
						func_call["args"] = nlohmann::json::parse(tc.function.arguments);
					} catch (...) {
						func_call["args"] = nlohmann::json::object();
					}

					nlohmann::json part_obj = {{"functionCall", func_call}};
					if (tc.signature) {
						part_obj["thoughtSignature"] = *tc.signature;
					}
					parts_array.push_back(part_obj);
				}
			} else {
				parts_array.push_back({{"text", msg.content}});
			}
		} else {
			if (msg.role == "user" && msg.content.find("images://") != std::string::npos) {
				parts_array = process_user_parts_gemini(msg.content);
			} else {
				parts_array.push_back({{"text", msg.content}});
			}
		}

		std::string r = "user";
		if (msg.role == "assistant") {
			r = "model";
		}

		if (!contents.empty() && contents.back()["role"] == r) {
			for (const auto &p : parts_array) {
				contents.back()["parts"].push_back(p);
			}
		} else {
			contents.push_back({{"role", r}, {"parts", parts_array}});
		}
	}

	payload["contents"] = contents;

	if (!system_instruction.empty()) {
		payload["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_instruction}}})}};
	}

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
		clean_gemini_schema(params);
		nlohmann::json func_decl = {{"name", tool_name}, {"description", desc}, {"parameters", params}};
		tools_array.push_back(func_decl);
	}

	if (!tools_array.empty()) {
		payload["tools"] = nlohmann::json::array({{{"functionDeclarations", tools_array}}});
	}

	std::string body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	std::string endpoint = "/v1beta/models/" + model_id_ + ":streamGenerateContent?alt=sse";

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
					if (chunk.is_array() && !chunk.empty()) {
						chunk = chunk[0];
					}

					if (chunk.contains("usageMetadata") && !chunk["usageMetadata"].is_null()) {
						auto usage = chunk["usageMetadata"];
						if (usage.contains("promptTokenCount"))
							final_usage.prompt_tokens = usage["promptTokenCount"].get<int>();
						if (usage.contains("cachedContentTokenCount"))
							final_usage.cached_tokens = usage["cachedContentTokenCount"].get<int>();
						if (usage.contains("candidatesTokenCount"))
							final_usage.completion_tokens = usage["candidatesTokenCount"].get<int>();
						if (usage.contains("totalTokenCount"))
							final_usage.total_tokens = usage["totalTokenCount"].get<int>();
					}

					if (chunk.contains("candidates") && !chunk["candidates"].empty()) {
						auto choice = chunk["candidates"][0];
						if (choice.contains("content")) {
							auto content = choice["content"];
							std::string text;
							std::vector<tool_call> tool_calls;

							if (content.contains("parts") && !content["parts"].empty()) {
								for (const auto &part : content["parts"]) {
									if (part.contains("text")) {
										text += part["text"].get<std::string>();
									}
									if (part.contains("functionCall")) {
										auto fc = part["functionCall"];
										tool_call tc;
										tc.type = "function";
										tc.function.name = fc.value("name", "");
										if (fc.contains("args")) {
											tc.function.arguments = fc["args"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
										}
										if (part.contains("thoughtSignature")) {
											tc.signature = part["thoughtSignature"].get<std::string>();
										}
										tool_calls.push_back(tc);
									}
								}
							}

							if (!text.empty()) {
								stream_event event{ stream_event::event_type::content_chunk, text, {}, {}, final_response_id };
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

} // namespace agentlib
