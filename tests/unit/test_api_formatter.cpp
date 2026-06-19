#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/protocols/connection.h"
#include "../../src/agentlib/protocols/openai_completion_connection.h"
#include "../../src/agentlib/protocols/openai_response_connection.h"
#include "../../src/agentlib/protocols/gemini_connection.h"
#include "../../src/agentlib/protocols/connection_factory.h"
#include "../../src/agentlib/data/conversation.h"
#include "../../src/agentlib/data/system_turn.h"
#include "../../src/agentlib/data/user_turn.h"
#include "../../src/agentlib/data/model_response_turn.h"
#include "../../src/agentlib/data/tool_execution_turn.h"
#include "../../src/agentlib/data/transaction.h"
#include "../../src/agentlib/tool_registry.h"

using namespace agentlib;
using json = nlohmann::json;

class mock_compress_validator : public tool_validator
{
      public:
	std::string get_name() const override { return "agent_compress_history"; }
	std::string get_description() const override { return "mock"; }
	std::string get_family() const override { return "base"; }
	nlohmann::json get_parameters_schema() const override { return {}; }
	bool is_pure() const override { return false; }
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override { return nullptr; }
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override { return true; }
};

class mock_normal_validator : public tool_validator
{
      public:
	std::string get_name() const override { return "mock_normal_tool"; }
	std::string get_description() const override { return "mock"; }
	std::string get_family() const override { return "base"; }
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"path", {{"type", "string"}}}, {"async", {{"type", "boolean"}}}}},
			{"required", {"path", "async"}}};
	}
	bool is_pure() const override { return false; }
	std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json &) const override { return nullptr; }
	bool validate_args_impl(const nlohmann::json &, const tool_context &, std::string &) const override { return true; }
};

class mock_transport : public llm_transport {
public:
	std::string last_path;
	std::string last_body;
	transport_response mock_res;
	bool stream_success = true;
	std::vector<std::string> stream_chunks;

	transport_response post(const std::string& path, const std::string& json_body) override {
		last_path = path;
		last_body = json_body;
		return mock_res;
	}

	bool post_stream(const std::string& path, const std::string& json_body, 
			 std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) override {
		last_path = path;
		last_body = json_body;
		if (!stream_success) return false;
		for (const auto& chunk : stream_chunks) {
			callback(chunk.data(), chunk.size(), 0, chunk.size());
		}
		return true;
	}

	std::string get_base_url() const override { return "mock://"; }
	std::string get_last_error() const override { return ""; }
};

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Running test_api_formatter..." << std::endl;

	auto &registry = tool_registry::get_instance();
	registry.register_validator([]() { return std::make_unique<mock_compress_validator>(); });
	registry.register_validator([]() { return std::make_unique<mock_normal_validator>(); });

	auto convo = std::make_shared<Conversation>();
	auto ep = convo->create_new_episode("temp_ep_id", "temp_ep_title", "temp_ep_summary");
	auto tx = std::make_shared<Transaction>("temp_tx_id", transaction_type::user_exchange);
	ep->add_transaction(tx);
	tx->add_turn(std::make_shared<system_turn>("1", "First system instruction.", "base"));
	tx->add_turn(std::make_shared<system_turn>("2", "Second system instruction.", "base"));
	tx->add_turn(std::make_shared<user_turn>("3", "Hello"));

	// 1. Test Gemini payload formatting
	{
		auto transport = std::make_shared<mock_transport>();
		gemini_connection gemini_conn(transport, "gemini-1.5-pro", api_type::gemini);

		gemini_conn.send_prompt(*convo, "developer", {}, [](const stream_event&){});
		assert(transport->last_path == "/v1beta/models/gemini-1.5-pro:streamGenerateContent?alt=sse");
		json payload = json::parse(transport->last_body);
		assert(payload.contains("systemInstruction"));
		std::string combined_text = payload["systemInstruction"]["parts"][0]["text"].get<std::string>();
		assert(combined_text.find("First system instruction.") != std::string::npos);
		assert(combined_text.find("Second system instruction.") != std::string::npos);
	}

	// 2. Test OpenAI Completion/Copilot/Response paths
	{
		auto t1 = std::make_shared<mock_transport>();
		openai_completion_connection c1(t1, "gpt-4", api_type::openai);
		c1.send_prompt(*convo, "developer", {}, [](const stream_event&){});
		assert(t1->last_path == "/v1/chat/completions");
	}
	{
		auto t2 = std::make_shared<mock_transport>();
		openai_completion_connection c2(t2, "gpt-4", api_type::copilot);
		c2.send_prompt(*convo, "developer", {}, [](const stream_event&){});
		assert(t2->last_path == "/chat/completions");
	}
	{
		auto t3 = std::make_shared<mock_transport>();
		openai_response_connection c3(t3, "gpt-4", api_type::openai_response);
		c3.send_prompt(*convo, "developer", {}, [](const stream_event&){});
		assert(t3->last_path == "/v1/responses");
	}

	// 3. Test tools filtering on OpenAI Response
	{
		auto t = std::make_shared<mock_transport>();
		openai_response_connection c(t, "gpt-4", api_type::openai_response);
		c.send_prompt(*convo, "developer", {}, [](const stream_event&){});
		json payload_resp = json::parse(t->last_body);
		assert(payload_resp.contains("instructions"));
		assert(payload_resp["instructions"].get<std::string>() == "First system instruction.\n\nSecond system instruction.");
		assert(payload_resp.contains("input"));
		assert(payload_resp["input"].is_array());
		
		assert(payload_resp.contains("tools"));
		bool found_compress = false;
		bool found_flat_mock_tool = false;
		for (const auto &tool : payload_resp["tools"]) {
			if (tool.contains("name")) {
				std::string name = tool["name"].get<std::string>();
				if (name == "agent_compress_history") {
					found_compress = true;
				}
				if (name == "mock_normal_tool") {
					found_flat_mock_tool = true;
					assert(tool.contains("type") && tool["type"] == "function");
					assert(!tool.contains("function")); // flat
					assert(tool.contains("parameters"));
					auto params = tool["parameters"];
					assert(!params["properties"].contains("async"));
				}
			}
		}
		assert(!found_compress);
		assert(found_flat_mock_tool);
	}

	// 4. Test parsing stream delta events from Responses API
	{
		auto t = std::make_shared<mock_transport>();
		t->stream_chunks = {
			"data: {\"type\": \"response.output_text.delta\", \"delta\": \"Hello\"}\n",
			"data: {\"type\": \"response.reasoning_summary_text.delta\", \"delta\": \"Thinking\"}\n",
			"data: {\"type\": \"response.output_item.added\", \"item\": {\"type\": \"function_call\", \"call_id\": \"call_xyz\", \"name\": \"fs_read_lines\", \"arguments\": \"\"}}\n",
			"data: {\"type\": \"response.function_call_arguments.delta\", \"delta\": \"{\\\"\"}\n",
			"data: {\"type\": \"response.completed\", \"response\": {\"id\": \"resp_123\", \"usage\": {\"input_tokens\": 10, \"output_tokens\": 20, \"total_tokens\": 30}}}\n"
		};

		openai_response_connection c(t, "gpt-4", api_type::openai_response);
		std::vector<stream_event> events;
		c.send_prompt(*convo, "developer", {}, [&](const stream_event& ev) {
			events.push_back(ev);
		});

		assert(events.size() == 5);
		assert(events[0].type == stream_event::event_type::content_chunk);
		assert(events[0].text == "Hello");
		
		assert(events[1].type == stream_event::event_type::reasoning_chunk);
		assert(events[1].text == "Thinking");

		assert(events[2].type == stream_event::event_type::tool_call_delta);
		assert(events[2].tool_calls.size() == 1);
		assert(events[2].tool_calls[0].id == "call_xyz");
		assert(events[2].tool_calls[0].function.name == "fs_read_lines");

		assert(events[3].type == stream_event::event_type::tool_call_delta);
		assert(events[3].tool_calls.size() == 1);
		assert(events[3].tool_calls[0].function.arguments == "{\"");

		assert(events[4].type == stream_event::event_type::completed);
		assert(events[4].response_id == "resp_123");
		assert(events[4].usage.prompt_tokens == 10);
		assert(events[4].usage.completion_tokens == 20);
		assert(events[4].usage.total_tokens == 30);
	}

	std::cout << "test_api_formatter passed successfully!" << std::endl;
	return 0;
}
