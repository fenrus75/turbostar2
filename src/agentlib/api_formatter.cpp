#include "api_formatter.h"
#include <iostream>

using json = nlohmann::json;

namespace agentlib
{

std::unique_ptr<api_formatter> api_formatter::create(api_type type)
{
	if (type == api_type::gemini) {
		return std::make_unique<gemini_formatter>();
	}
	return std::make_unique<openai_formatter>();
}

// --- openai_formatter ---

std::string openai_formatter::get_endpoint_path(const std::string & /*model_id*/, bool /*stream*/) const
{
	// OpenAI and its shims typically use the standard chat completions path
	return "/v1/chat/completions";
}

std::string openai_formatter::build_chat_payload(const std::string &model_id, const std::vector<message> &convo,
						 const tool_registry *registry, bool stream,
						 const std::vector<std::string> &active_families) const
{
	json msgs_json = json::array();
	for (const auto &msg : convo) {
		json m_json;
		to_json(m_json, msg);
		m_json.erase("episode_id");
		m_json.erase("episode_level");
		msgs_json.push_back(m_json);
	}
	json payload = {{"model", model_id}, {"messages", msgs_json}, {"stream", stream}};

	if (stream) {
		payload["stream_options"] = {{"include_usage", true}};
	}

	if (registry) {
		json tools_json = registry->get_tools_json(active_families);
		if (!tools_json.empty()) {
			payload["tools"] = tools_json;
			payload["tool_choice"] = "auto";
		}
	}
	return payload.dump();
}

chat_delta openai_formatter::parse_stream_chunk(const std::string &json_chunk) const
{
	chat_delta delta;
	if (json_chunk == "[DONE]") {
		delta.is_final = true;
		return delta;
	}

	try {
		json chunk = json::parse(json_chunk);

		if (chunk.contains("usage") && !chunk["usage"].is_null()) {
			auto usage = chunk["usage"];
			if (usage.contains("prompt_tokens"))
				delta.usage.prompt_tokens = usage["prompt_tokens"].get<int>();
			if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].contains("cached_tokens"))
				delta.usage.cached_tokens = usage["prompt_tokens_details"]["cached_tokens"].get<int>();
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
	} catch (...) {
		// Ignore malformed chunks
	}
	return delta;
}

llm_chat_response openai_formatter::parse_sync_response(const std::string &json_body) const
{
	llm_chat_response chat_response;
	chat_response.msg.role = "assistant";
	try {
		json response = json::parse(json_body);
		if (response.contains("model")) {
			chat_response.model = response["model"].get<std::string>();
		}
		if (response.contains("usage")) {
			auto usage = response["usage"];
			if (usage.contains("prompt_tokens"))
				chat_response.usage.prompt_tokens = usage["prompt_tokens"].get<int>();
			if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].contains("cached_tokens"))
				chat_response.usage.cached_tokens = usage["prompt_tokens_details"]["cached_tokens"].get<int>();
			if (usage.contains("completion_tokens"))
				chat_response.usage.completion_tokens = usage["completion_tokens"].get<int>();
			if (usage.contains("total_tokens"))
				chat_response.usage.total_tokens = usage["total_tokens"].get<int>();
		}
		if (response.contains("choices") && !response["choices"].empty()) {
			auto msg_json = response["choices"][0]["message"];
			chat_response.msg = msg_json.get<message>();
		}
	} catch (const std::exception &e) {
		chat_response.msg.content = "Error parsing response JSON: " + std::string(e.what());
	}
	return chat_response;
}

// --- gemini_formatter ---

std::string gemini_formatter::get_endpoint_path(const std::string &model_id, bool stream) const
{
	std::string path = "/v1beta/models/" + model_id;
	if (stream) {
		path += ":streamGenerateContent?alt=sse";
	} else {
		path += ":generateContent";
	}
	return path;
}

std::string gemini_formatter::build_chat_payload(const std::string &model_id, const std::vector<message> &convo,
						 const tool_registry *registry, bool stream,
						 const std::vector<std::string> &active_families) const
{
	(void)model_id; // Gemini expects model in the URL path, not payload
	(void)stream;
	json payload = json::object();

	json contents = json::array();
	for (const auto &msg : convo) {
		json part = json::object();

		if (msg.role == "tool") {
			// Fix the Gemini Tool Response Bug
			json function_response = json::object();
			function_response["name"] = msg.name ? *msg.name : "";

			// Try to parse the content as JSON, otherwise wrap it
			try {
				function_response["response"] = {{"result", json::parse(msg.content)}};
			} catch (...) {
				function_response["response"] = {{"result", msg.content}};
			}

			part["functionResponse"] = function_response;
		} else if (msg.role == "assistant") {
			if (msg.tool_calls && !msg.tool_calls->empty()) {
				json parts_array = json::array();
				if (!msg.content.empty()) {
					parts_array.push_back({{"text", msg.content}});
				}
				for (const auto &tc : *msg.tool_calls) {
					json func_call = {{"name", tc.function.name}};
					try {
						func_call["args"] = json::parse(tc.function.arguments);
					} catch (...) {
						func_call["args"] = json::object();
					}

					json part_obj = {{"functionCall", func_call}};
					if (tc.signature) {
						part_obj["thoughtSignature"] = *tc.signature;
					}
					parts_array.push_back(part_obj);
				}
				json content_obj = {{"role", "model"}, {"parts", parts_array}};
				contents.push_back(content_obj);
				continue; // Skip the default push
			} else {
				part["text"] = msg.content;
			}
		} else {
			// User or system
			part["text"] = msg.content;
		}

		std::string role = "user";
		if (msg.role == "assistant")
			role = "model";
		else if (msg.role == "tool")
			role = "function";

		if (msg.role == "system") {
			// Gemini handles system instruction separately, but if we inline it:
			payload["systemInstruction"] = {{"parts", json::array({{{"text", msg.content}}})}};
			continue;
		}

		contents.push_back({{"role", role}, {"parts", json::array({part})}});
	}

	payload["contents"] = contents;

	json tools_array = json::array();

	if (registry) {
		json local_tools = registry->get_gemini_tools_json(active_families);
		if (!local_tools.empty()) {
			// local_tools is already an array of tool objects (e.g. [{"functionDeclarations": [...]}])
			for (const auto &item : local_tools) {
				tools_array.push_back(item);
			}
		}
	}

	if (!tools_array.empty()) {
		payload["tools"] = tools_array;
	}

	return payload.dump();
}

chat_delta gemini_formatter::parse_stream_chunk(const std::string &json_chunk) const
{
	chat_delta delta;
	if (json_chunk.empty() || json_chunk == "[DONE]") {
		return delta;
	}

	try {
		json chunk = json::parse(json_chunk);

		// Gemini streaming chunks are often array elements
		if (chunk.is_array() && !chunk.empty()) {
			chunk = chunk[0];
		}

		if (chunk.contains("usageMetadata") && !chunk["usageMetadata"].is_null()) {
			auto usage = chunk["usageMetadata"];
			if (usage.contains("promptTokenCount"))
				delta.usage.prompt_tokens = usage["promptTokenCount"].get<int>();
			if (usage.contains("cachedContentTokenCount"))
				delta.usage.cached_tokens = usage["cachedContentTokenCount"].get<int>();
			if (usage.contains("candidatesTokenCount"))
				delta.usage.completion_tokens = usage["candidatesTokenCount"].get<int>();
			if (usage.contains("totalTokenCount"))
				delta.usage.total_tokens = usage["totalTokenCount"].get<int>();
		}

		if (chunk.contains("candidates") && !chunk["candidates"].empty()) {
			auto choice = chunk["candidates"][0];

			if (choice.contains("content")) {
				auto content = choice["content"];
				if (content.contains("role")) {
					delta.role = content["role"].get<std::string>() == "model" ? "assistant" : "user";
				}

				if (content.contains("parts") && !content["parts"].empty()) {
					for (const auto &part : content["parts"]) {
						if (part.contains("text")) {
							delta.content += part["text"].get<std::string>();
						}
						if (part.contains("functionCall")) {
							auto fc = part["functionCall"];
							tool_call tc;
							tc.type = "function";
							tc.function.name = fc.value("name", "");
							if (fc.contains("args")) {
								tc.function.arguments = fc["args"].dump();
							}
							if (part.contains("thoughtSignature")) {
								tc.signature = part["thoughtSignature"].get<std::string>();
							}
							if (!delta.tool_calls)
								delta.tool_calls = std::vector<tool_call>();
							delta.tool_calls->push_back(tc);
						}
					}
				}
			}
			if (choice.contains("finishReason") && !choice["finishReason"].is_null()) {
				delta.is_final = true;
			}
		}
	} catch (...) {
		// Ignore malformed chunks
	}
	return delta;
}

llm_chat_response gemini_formatter::parse_sync_response(const std::string &json_body) const
{
	llm_chat_response chat_response;
	chat_response.msg.role = "assistant";
	try {
		json chunk = json::parse(json_body);

		if (chunk.contains("usageMetadata") && !chunk["usageMetadata"].is_null()) {
			auto usage = chunk["usageMetadata"];
			if (usage.contains("promptTokenCount"))
				chat_response.usage.prompt_tokens = usage["promptTokenCount"].get<int>();
			if (usage.contains("cachedContentTokenCount"))
				chat_response.usage.cached_tokens = usage["cachedContentTokenCount"].get<int>();
			if (usage.contains("candidatesTokenCount"))
				chat_response.usage.completion_tokens = usage["candidatesTokenCount"].get<int>();
			if (usage.contains("totalTokenCount"))
				chat_response.usage.total_tokens = usage["totalTokenCount"].get<int>();
		}

		if (chunk.contains("candidates") && !chunk["candidates"].empty()) {
			auto choice = chunk["candidates"][0];

			if (choice.contains("content")) {
				auto content = choice["content"];

				if (content.contains("parts") && !content["parts"].empty()) {
					for (const auto &part : content["parts"]) {
						if (part.contains("text")) {
							chat_response.msg.content += part["text"].get<std::string>();
						}
						if (part.contains("functionCall")) {
							auto fc = part["functionCall"];
							tool_call tc;
							tc.type = "function";
							tc.function.name = fc.value("name", "");
							if (fc.contains("args")) {
								tc.function.arguments = fc["args"].dump();
							}
							if (part.contains("thoughtSignature")) {
								tc.signature = part["thoughtSignature"].get<std::string>();
							}
							if (!chat_response.msg.tool_calls)
								chat_response.msg.tool_calls = std::vector<tool_call>();
							chat_response.msg.tool_calls->push_back(tc);
						}
					}
				}
			}
		}
	} catch (const std::exception &e) {
		chat_response.msg.content = "Error parsing response JSON: " + std::string(e.what());
	}
	return chat_response;
}

} // namespace agentlib