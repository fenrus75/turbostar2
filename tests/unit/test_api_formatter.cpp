#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/api_formatter.h"

using namespace agentlib;
using json = nlohmann::json;

class mock_compress_validator : public tool_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_compress_history";
	}
	std::string get_description() const override
	{
		return "mock";
	}
	std::string get_family() const override
	{
		return "base";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {};
	}
	bool is_pure() const override
	{
		return false;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return nullptr;
	}
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
};

class mock_normal_validator : public tool_validator
{
      public:
	std::string get_name() const override
	{
		return "mock_normal_tool";
	}
	std::string get_description() const override
	{
		return "mock";
	}
	std::string get_family() const override
	{
		return "base";
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"path", {{"type", "string"}}}, {"async", {{"type", "boolean"}}}}},
			{"required", {"path", "async"}}};
	}
	bool is_pure() const override
	{
		return false;
	}
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override
	{
		return nullptr;
	}
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override
	{
		return true;
	}
};

int main()
{
	std::cout << "Running test_api_formatter..." << std::endl;

	// Register mocks
	auto &registry = tool_registry::get_instance();
	registry.register_validator([]() { return std::make_unique<mock_compress_validator>(); });
	registry.register_validator([]() { return std::make_unique<mock_normal_validator>(); });

	auto formatter = api_formatter::create(api_type::gemini);
	assert(formatter != nullptr);

	std::vector<message> convo;
	message msg1;
	msg1.role = "system";
	msg1.content = "First system instruction.";
	convo.push_back(msg1);

	message msg2;
	msg2.role = "system";
	msg2.content = "Second system instruction.";
	convo.push_back(msg2);

	message msg3;
	msg3.role = "user";
	msg3.content = "Hello";
	convo.push_back(msg3);

	std::string payload_str = formatter->build_chat_payload("gemini-1.5-pro", convo, nullptr, false);
	std::cout << "Generated payload: " << payload_str << std::endl;

	json payload = json::parse(payload_str);
	assert(payload.contains("systemInstruction"));
	assert(payload["systemInstruction"].contains("parts"));
	assert(!payload["systemInstruction"]["parts"].empty());

	std::string combined_text = payload["systemInstruction"]["parts"][0]["text"].get<std::string>();
	std::cout << "System Instruction text: " << combined_text << std::endl;

	// Assert both system messages are present in the Gemini payload
	assert(combined_text.find("First system instruction.") != std::string::npos);
	assert(combined_text.find("Second system instruction.") != std::string::npos);

	// Test OpenAI formatter endpoint path
	auto open_fmt = api_formatter::create(api_type::openai);
	assert(open_fmt != nullptr);
	assert(open_fmt->get_endpoint_path("gpt-4", false) == "/v1/chat/completions");

	// Test openai_response formatter
	auto resp_fmt = api_formatter::create(api_type::openai_response);
	assert(resp_fmt != nullptr);
	assert(resp_fmt->get_endpoint_path("gpt-4", false) == "/v1/responses");

	// Test Copilot formatter endpoint path
	auto cop_fmt = api_formatter::create(api_type::copilot);
	assert(cop_fmt != nullptr);
	assert(cop_fmt->get_endpoint_path("gpt-4", false) == "/chat/completions");

	// Test tools filtering on openai_response
	json normal_tools = registry.get_tools_json({}, true);
	json resp_tools = registry.get_tools_json({}, false);

	bool has_compress_normal = false;
	for (const auto &t : normal_tools) {
		if (t["function"]["name"] == "agent_compress_history") {
			has_compress_normal = true;
		}
	}
	assert(has_compress_normal);

	bool has_compress_resp = false;
	for (const auto &t : resp_tools) {
		if (t["function"]["name"] == "agent_compress_history" || t["function"]["name"] == "agent_restore_context") {
			has_compress_resp = true;
		}
	}
	assert(!has_compress_resp);

	bool found_normal_tool = false;
	for (const auto &t : resp_tools) {
		if (t["function"]["name"] == "mock_normal_tool") {
			found_normal_tool = true;
			auto params = t["function"]["parameters"];
			assert(params.contains("properties"));
			assert(params["properties"].contains("path"));
			assert(!params["properties"].contains("async"));
			assert(params.contains("required"));
			bool has_async_in_req = false;
			for (const auto &req : params["required"]) {
				if (req == "async") {
					has_async_in_req = true;
				}
			}
			assert(!has_async_in_req);
		}
	}
	assert(found_normal_tool);

	// Test openai_response payload formatting
	std::string payload_str_resp = resp_fmt->build_chat_payload("gpt-4", convo, &registry, false);
	json payload_resp = json::parse(payload_str_resp);
	assert(payload_resp.contains("instructions"));
	assert(payload_resp["instructions"].get<std::string>() == "First system instruction.\n\nSecond system instruction.");
	assert(payload_resp.contains("input"));
	assert(payload_resp["input"].is_array());
	assert(payload_resp["input"].size() == 1);
	assert(payload_resp["input"][0]["type"] == "message");
	assert(payload_resp["input"][0]["role"] == "user");
	assert(payload_resp["input"][0]["content"].is_array());
	assert(payload_resp["input"][0]["content"].size() == 1);
	assert(payload_resp["input"][0]["content"][0]["type"] == "input_text");
	assert(payload_resp["input"][0]["content"][0]["text"] == "Hello");

	// Test flat tools structure in openai_response payload
	assert(payload_resp.contains("tools"));
	assert(payload_resp["tools"].is_array());
	bool found_flat_mock_tool = false;
	for (const auto &t : payload_resp["tools"]) {
		if (t.contains("name") && t["name"] == "mock_normal_tool") {
			found_flat_mock_tool = true;
			assert(t.contains("type") && t["type"] == "function");
			assert(!t.contains("function")); // should be flat
			assert(t.contains("description"));
			assert(t.contains("parameters"));
		}
	}
	assert(found_flat_mock_tool);

	// Test parsing sync response from Responses API
	std::string sync_resp_json = R"({
		"id": "resp_123",
		"object": "response",
		"model": "gpt-5.5",
		"output": [
			{
				"id": "msg_1",
				"type": "message",
				"role": "assistant",
				"content": [
					{
						"type": "output_text",
						"text": "Answer text content.",
						"annotations": []
					}
				]
			},
			{
				"type": "reasoning",
				"text": "Thinking process."
			},
			{
				"call_id": "call_abc",
				"name": "get_weather",
				"arguments": "{\"location\":\"Paris\"}",
				"type": "function_call"
			}
		],
		"usage": {
			"input_tokens": 100,
			"output_tokens": 200,
			"total_tokens": 300
		}
	})";
	llm_chat_response parsed_resp = resp_fmt->parse_sync_response(sync_resp_json);
	assert(parsed_resp.model == "gpt-5.5");
	assert(parsed_resp.msg.role == "assistant");
	assert(parsed_resp.msg.content == "Answer text content.");
	assert(parsed_resp.msg.reasoning_content && *parsed_resp.msg.reasoning_content == "Thinking process.");
	assert(parsed_resp.msg.tool_calls && parsed_resp.msg.tool_calls->size() == 1);
	assert((*parsed_resp.msg.tool_calls)[0].id == "call_abc");
	assert((*parsed_resp.msg.tool_calls)[0].function.name == "get_weather");
	assert((*parsed_resp.msg.tool_calls)[0].function.arguments == "{\"location\":\"Paris\"}");
	assert(parsed_resp.usage.prompt_tokens == 100);
	assert(parsed_resp.usage.completion_tokens == 200);
	assert(parsed_resp.usage.total_tokens == 300);

	// Test parsing stream chunks from Responses API
	chat_delta d1 = resp_fmt->parse_stream_chunk(R"({"type": "response.output_text.delta", "delta": "Hello"})");
	assert(d1.content == "Hello");
	assert(d1.reasoning_content == "");
	assert(!d1.is_final);

	chat_delta d2 = resp_fmt->parse_stream_chunk(R"({"type": "response.reasoning_summary_text.delta", "delta": "Thinking"})");
	assert(d2.content == "");
	assert(d2.reasoning_content == "Thinking");
	assert(!d2.is_final);

	chat_delta d3 = resp_fmt->parse_stream_chunk(R"({
		"type": "response.output_item.added",
		"item": {
			"type": "function_call",
			"call_id": "call_xyz",
			"name": "fs_read_lines",
			"arguments": ""
		}
	})");
	assert(d3.tool_calls && d3.tool_calls->size() == 1);
	assert((*d3.tool_calls)[0].id == "call_xyz");
	assert((*d3.tool_calls)[0].function.name == "fs_read_lines");

	chat_delta d4 = resp_fmt->parse_stream_chunk(R"({"type": "response.function_call_arguments.delta", "delta": "{\""})");
	assert(d4.tool_calls && d4.tool_calls->size() == 1);
	assert((*d4.tool_calls)[0].id == "");
	assert((*d4.tool_calls)[0].function.arguments == "{\"");

	chat_delta d5 = resp_fmt->parse_stream_chunk(
	    R"({"type": "response.completed", "response": {"usage": {"input_tokens": 10, "output_tokens": 20, "total_tokens": 30}}})");
	assert(d5.is_final);
	assert(d5.usage.prompt_tokens == 10);
	assert(d5.usage.completion_tokens == 20);
	assert(d5.usage.total_tokens == 30);

	std::cout << "test_api_formatter passed successfully!" << std::endl;
	return 0;
}
