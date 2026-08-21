// Tested source file: src/agentlib/protocols/openai_completion_connection.cpp
#include "test_watchdog.h"
#include <cassert>
#include <fstream>
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
		for (const auto& chunk : stream_chunks) {
			callback(chunk.data(), chunk.size(), 0, chunk.size());
		}
		return stream_success;
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

		gemini_conn.send_prompt(*convo, agent_properties{}, [](const stream_event&){});
		assert(transport->last_path == "/v1beta/models/gemini-1.5-pro:streamGenerateContent?alt=sse");
		json payload = json::parse(transport->last_body);
		assert(payload.contains("systemInstruction"));
		std::string combined_text = payload["systemInstruction"]["parts"][0]["text"].get<std::string>();
		assert(combined_text.find("First system instruction.") != std::string::npos);
		std::string contents_str = payload["contents"].dump();
		assert(contents_str.find("Second system instruction.") != std::string::npos);
	}

	// 2. Test OpenAI Completion/Copilot/Response paths
	{
		auto t1 = std::make_shared<mock_transport>();
		openai_completion_connection c1(t1, "gpt-4", api_type::openai);
		c1.send_prompt(*convo, agent_properties{}, [](const stream_event&){});
		assert(t1->last_path == "/v1/chat/completions");
	}
	{
		auto t2 = std::make_shared<mock_transport>();
		openai_completion_connection c2(t2, "gpt-4", api_type::copilot);
		c2.send_prompt(*convo, agent_properties{}, [](const stream_event&){});
		assert(t2->last_path == "/chat/completions");
	}
	{
		auto t3 = std::make_shared<mock_transport>();
		openai_response_connection c3(t3, "gpt-4", api_type::openai_response);
		c3.send_prompt(*convo, agent_properties{}, [](const stream_event&){});
		assert(t3->last_path == "/v1/responses");
	}

	// 3. Test tools filtering on OpenAI Response
	{
		auto t = std::make_shared<mock_transport>();
		openai_response_connection c(t, "gpt-4", api_type::openai_response);
		c.send_prompt(*convo, agent_properties{}, [](const stream_event&){});
		json payload_resp = json::parse(t->last_body);
		assert(payload_resp.contains("instructions"));
		assert(payload_resp["instructions"].get<std::string>() == "First system instruction.");
		assert(payload_resp.contains("input"));
		assert(payload_resp["input"].is_array());
		assert(payload_resp["input"].dump().find("Second system instruction.") != std::string::npos);
		
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
		c.send_prompt(*convo, agent_properties{}, [&](const stream_event& ev) {
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

	// 5. Test tool call sequence protection against interleaved system messages
	{
		auto convo_seq = std::make_shared<Conversation>();
		auto ep_seq = convo_seq->create_new_episode("ep_seq", "seq_title", "seq_summary");
		auto tx_base = std::make_shared<Transaction>("tx_base", transaction_type::user_exchange);
		ep_seq->add_transaction(tx_base);
		tx_base->add_turn(std::make_shared<system_turn>("sys_0", "Base System Prompt.", "base"));
		tx_base->add_turn(std::make_shared<user_turn>("u_1", "Read file"));

		tool_call tc;
		tc.id = "call_abc";
		tc.type = "function";
		tc.function.name = "fs_read_lines";
		tc.function.arguments = "{}";

		auto tx_ast = std::make_shared<Transaction>("tx_ast", transaction_type::user_exchange);
		ep_seq->add_transaction(tx_ast);
		tx_ast->add_turn(std::make_shared<model_response_turn>("ast_2", "", std::nullopt, std::vector<tool_call>{tc}));

		// Inject mid-stream system message directly between assistant tool call and tool result
		auto tx_sys = std::make_shared<Transaction>("tx_sys", transaction_type::system_injection);
		ep_seq->add_transaction(tx_sys);
		tx_sys->add_turn(std::make_shared<system_turn>("sys_interleaved", "Auto-Episode Boundary", "boundary"));

		// Tool response turn comes after the system message
		tool_result tr;
		tr.call_id = "call_abc";
		tr.name = "fs_read_lines";
		tr.content = "file content line 1";
		auto tx_tool = std::make_shared<Transaction>("tx_tool", transaction_type::user_exchange);
		ep_seq->add_transaction(tx_tool);
		tx_tool->add_turn(std::make_shared<tool_execution_turn>("tool_3", std::vector<tool_result>{tr}));

		auto t_seq = std::make_shared<mock_transport>();
		openai_completion_connection c_seq(t_seq, "gpt-4", api_type::openai);
		c_seq.send_prompt(*convo_seq, agent_properties{}, [](const stream_event&){});

		json payload = json::parse(t_seq->last_body);
		auto msgs = payload["messages"];

		// Verify strict sequence: assistant (tool_calls) MUST be immediately followed by tool result
		size_t ast_idx = 0;
		for (size_t i = 0; i < msgs.size(); ++i) {
			if (msgs[i]["role"] == "assistant" && msgs[i].contains("tool_calls")) {
				ast_idx = i;
				break;
			}
		}
		assert(ast_idx > 0);
		assert(msgs[ast_idx + 1]["role"] == "tool");
		assert(msgs[ast_idx + 1]["tool_call_id"] == "call_abc");
		// The interleaved system message must appear AFTER the tool result
		assert(msgs[ast_idx + 2]["role"] == "system");
		assert(msgs[ast_idx + 2]["content"] == "Auto-Episode Boundary");
	}

	// 6. Test loading agent_chat_2.json and generating openai_completion_connection payload
	{
		std::string chat_path = "/home/arjan/.cache/turbostar/projects/10101618844961150461/tmp/agent_chat_2.json";
		std::ifstream ifs(chat_path);
		if (ifs.is_open()) {
			std::cout << "Loading saved conversation: " << chat_path << std::endl;
			nlohmann::json root = nlohmann::json::parse(ifs);
			ifs.close();

			auto loaded_convo = Conversation::deserialize(root["conversation"]);
			auto transport = std::make_shared<mock_transport>();
			openai_completion_connection conn(transport, "nvidia/MiniMax-M2.7-NVFP4", api_type::openai);
			conn.send_prompt(*loaded_convo, agent_properties{}, [](const stream_event&){});

			std::cout << "Generated payload body length: " << transport->last_body.size() << " bytes." << std::endl;
			std::cout << "First 100 bytes of body: [" << transport->last_body.substr(0, 100) << "]" << std::endl;

			try {
				nlohmann::json parsed_body = nlohmann::json::parse(transport->last_body);
				std::cout << "Successfully parsed generated payload JSON body! Messages count: " 
				          << parsed_body["messages"].size() << std::endl;

				// Scan transport->last_body for unescaped control characters inside JSON strings!
				bool in_string = false;
				bool escaped = false;
				size_t line = 1, col = 0;
				for (size_t i = 0; i < transport->last_body.size(); ++i) {
					char c = transport->last_body[i];
					col++;
					if (c == '\n') { line++; col = 0; }

					if (in_string) {
						if (escaped) {
							escaped = false;
						} else if (c == '\\') {
							escaped = true;
						} else if (c == '"') {
							in_string = false;
						} else if (static_cast<unsigned char>(c) < 0x20) {
							std::cout << "CRITICAL PROTOCOL BUG: Raw unescaped control char 0x"
								  << std::hex << static_cast<int>(static_cast<unsigned char>(c)) << std::dec
								  << " found in JSON body string at index " << i
								  << " (line " << line << " col " << col << ")!" << std::endl;
						}
					} else {
						if (c == '"') {
							in_string = true;
						}
					}
				}
			} catch (const std::exception &e) {
				std::cerr << "FAIL parsing generated payload body: " << e.what() << std::endl;
				assert(false && "Generated payload JSON body is invalid!");
			}

		}
	}

	// 7. Test repair_json_string on unterminated/truncated tool_call arguments
	{
		std::string truncated_args = "{\"path\": \"src/main.cpp\", \"target_content\": \"int main() {\\n    return 0;";
		std::string repaired = fs_utils::repair_json_string(truncated_args);
		json parsed = json::parse(repaired);
		assert(parsed["path"] == "src/main.cpp");
		assert(parsed["target_content"] == "int main() {\n    return 0;");

		// Test serializing tool_call with truncated arguments
		tool_call tc;
		tc.id = "call_trunc_123";
		tc.type = "function";
		tc.function.name = "fs_replace_content";
		tc.function.arguments = truncated_args;

		json tc_json;
		to_json(tc_json, tc);
		assert(tc_json["function"]["arguments"].is_string());
		json inner_args = json::parse(tc_json["function"]["arguments"].get<std::string>());
		assert(inner_args["path"] == "src/main.cpp");
	}

	// 8. Test stream completion when parsed_sse is true but transport returns false on socket close
	{
		auto transport = std::make_shared<mock_transport>();
		transport->stream_success = false;
		transport->stream_chunks.push_back("data: {\"id\":\"resp_sse_1\",\"choices\":[{\"delta\":{\"content\":\"Hello world\"}}]}\n\n");

		openai_completion_connection conn(transport, "gpt-4", api_type::openai);
		bool received_content = false;
		bool received_error = false;

		conn.send_prompt(*convo, agent_properties{}, [&](const stream_event& ev) {
			if (ev.type == stream_event::event_type::content_chunk) {
				if (ev.text == "Hello world") received_content = true;
			} else if (ev.type == stream_event::event_type::error) {
				received_error = true;
			}
		});

		assert(received_content && "Should have received content chunk from SSE stream");
		assert(!received_error && "Should NOT emit error event when SSE chunks were successfully parsed before socket close");
	}

	std::cout << "test_api_formatter passed successfully!" << std::endl;
	return 0;
}



