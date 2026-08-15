/*
 * test_llm_client.cpp
 *
 * Unit test for `src/agentlib/llm_client.cpp` (agentlib::llm_client).
 * Prior to this test, llm_client had ZERO dedicated unit-test coverage.
 *
 * Methods covered:
 *   - llm_client(std::shared_ptr<llm_transport>, std::string model_id, api_type)
 *   - llm_chat_response send_chat(std::span<const message>, const tool_registry*,
 *                                 std::string_view, const agent_properties&)
 *   - void send_chat_stream(std::span<const message>, std::function<void(const chat_delta&)>,
 *                           const tool_registry*, std::string_view, const agent_properties&)
 *   - void cancel()
 *   - std::string compact_response(const std::string&, std::string*)
 *
 * Approach:
 *   The mock transport below (canonical pattern copied from test_api_formatter.cpp)
 *   replays a canned OpenAI-compatible SSE body on post_stream() and a configurable
 *   JSON body on post(), so no network connection is required. send_chat() internally
 *   delegates to send_chat_stream(), which drives the connection's send_prompt()
 *   through connection_factory::create(). The tests assert both the per-delta delivery
 *   contract and the aggregated llm_chat_response produced by send_chat().
 */
#include <cassert>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "test_watchdog.h"
#include "agentlib/llm_client.h"
#include "agentlib/llm_transport.h"
#include "agentlib/llm_types.h"

using namespace agentlib;

// Minimal mock transport that captures the last request and replays canned SSE
// chunks to drive the streaming parser (identical pattern to test_api_formatter.cpp).
class mock_transport : public llm_transport {
      public:
	std::string last_path;
	std::string last_body;
	transport_response mock_res;
	bool stream_success = true;
	bool cancel_called = false;
	std::vector<std::string> stream_chunks;

	transport_response post(const std::string &path, const std::string &json_body) override
	{
		last_path = path;
		last_body = json_body;
		return mock_res;
	}

	bool post_stream(const std::string &path, const std::string &json_body,
			 std::function<bool(const char *data, size_t len, size_t off, size_t total)> callback) override
	{
		last_path = path;
		last_body = json_body;
		if (!stream_success) {
			return false;
		}
		for (const auto &chunk : stream_chunks) {
			callback(chunk.data(), chunk.size(), 0, chunk.size());
		}
		return true;
	}

	void cancel() override { cancel_called = true; }

	std::string get_base_url() const override { return "mock://"; }
	std::string get_last_error() const override { return ""; }
};

// Canned OpenAI chat.completions SSE stream exercising content chunks, a reasoning
// chunk, two separate tool_call deltas, a usage-only chunk and the [DONE] sentinel.
static void load_full_stream(std::shared_ptr<mock_transport> t)
{
	t->stream_chunks = {
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello\"}}]}\n",
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"reasoning_content\":\"Thinking\"}}]}\n",
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"fs_read_lines\",\"arguments\":\"{}\"}}]}}]}\n",
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"id\":\"call_2\",\"type\":\"function\",\"function\":{\"name\":\"fs_glob\",\"arguments\":\"{\\\"pattern\\\":\\\"*.cpp\\\"}\"}}]}}]}\n",
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\" world\"}}]}\n",
	    "data: {\"id\":\"chatcmpl-123\",\"object\":\"chat.completion.chunk\",\"choices\":[],\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n",
	    "data: [DONE]\n",
	};
}

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Running test_llm_client..." << std::endl;

	// --- 1. Constructor + send_chat() aggregation ---
	// send_chat() must fold every stream delta into a single llm_chat_response:
	// concatenated content, concatenated reasoning_content, appended tool_calls,
	// usage (from the chunk where total_tokens > 0) and the final response_id.
	{
		auto t = std::make_shared<mock_transport>();
		load_full_stream(t);

		llm_client client(t, "gpt-4o", api_type::openai);

		// Exercise build_temp_conversation() across system/user/assistant/tool roles.
		std::vector<message> msgs;
		message sys;
		sys.role = "system";
		sys.content = "You are a helpful assistant.";
		message usr;
		usr.role = "user";
		usr.content = "Read the file";
		tool_call tc;
		tc.id = "call_prev";
		tc.type = "function";
		tc.function.name = "fs_read_lines";
		tc.function.arguments = "{}";
		message ast;
		ast.role = "assistant";
		ast.tool_calls = std::vector<tool_call>{tc};
		message tl;
		tl.role = "tool";
		tl.tool_call_id = "call_prev";
		tl.name = "fs_read_lines";
		tl.content = "file content";
		msgs.push_back(sys);
		msgs.push_back(usr);
		msgs.push_back(ast);
		msgs.push_back(tl);

		auto resp = client.send_chat(msgs);

		assert(resp.msg.role == "assistant");
		assert(resp.msg.content == "Hello world");
		assert(resp.msg.reasoning_content.has_value());
		assert(*resp.msg.reasoning_content == "Thinking");
		assert(resp.msg.tool_calls.has_value());
		assert(resp.msg.tool_calls->size() == 2);
		assert((*resp.msg.tool_calls)[0].id == "call_1");
		assert((*resp.msg.tool_calls)[0].function.name == "fs_read_lines");
		assert((*resp.msg.tool_calls)[1].id == "call_2");
		assert((*resp.msg.tool_calls)[1].function.name == "fs_glob");
		assert((*resp.msg.tool_calls)[1].function.arguments == "{\"pattern\":\"*.cpp\"}");
		assert(resp.usage.total_tokens == 30);
		assert(resp.usage.prompt_tokens == 10);
		assert(resp.usage.completion_tokens == 20);
		assert(resp.response_id == "chatcmpl-123");
		assert(t->last_path == "/v1/chat/completions");
		std::cout << "  [1] send_chat aggregation OK" << std::endl;
	}

	// --- 2. send_chat_stream() per-delta delivery ---
	// Each content_chunk / reasoning_chunk / tool_call_delta must arrive as its own
	// chat_delta; the completed event sets is_final, usage and response_id.
	{
		auto t = std::make_shared<mock_transport>();
		load_full_stream(t);

		llm_client client(t, "gpt-4o", api_type::openai);
		message usr;
		usr.role = "user";
		usr.content = "Hi";
		std::vector<message> msgs{usr};

		std::vector<chat_delta> deltas;
		client.send_chat_stream(msgs, [&](const chat_delta &delta) { deltas.push_back(delta); });

		assert(deltas.size() == 6);
		// 0: content chunk
		assert(deltas[0].content == "Hello");
		assert(deltas[0].reasoning_content.empty());
		assert(!deltas[0].is_final);
		// 1: reasoning chunk
		assert(deltas[1].reasoning_content == "Thinking");
		assert(deltas[1].content.empty());
		// 2: first tool_call delta
		assert(deltas[2].tool_calls.has_value());
		assert(deltas[2].tool_calls->size() == 1);
		assert((*deltas[2].tool_calls)[0].id == "call_1");
		// 3: second tool_call delta
		assert(deltas[3].tool_calls.has_value());
		assert((*deltas[3].tool_calls)[0].id == "call_2");
		// 4: second content chunk
		assert(deltas[4].content == " world");
		// 5: completed event
		assert(deltas[5].is_final);
		assert(deltas[5].usage.total_tokens == 30);
		assert(deltas[5].response_id == "chatcmpl-123");
		std::cout << "  [2] send_chat_stream per-delta delivery OK" << std::endl;
	}

	// --- 3. Empty conversation ---
	// build_temp_conversation() with no messages must not crash and the stream
	// should still be delivered normally.
	{
		auto t = std::make_shared<mock_transport>();
		t->stream_chunks = {
		    "data: {\"id\":\"e1\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"hi\"}}]}\n",
		    "data: [DONE]\n",
		};
		llm_client client(t, "gpt-4o", api_type::openai);
		std::vector<message> empty;
		auto resp = client.send_chat(empty);
		assert(resp.msg.role == "assistant");
		assert(resp.msg.content == "hi");
		std::cout << "  [3] empty conversation OK" << std::endl;
	}

	// --- 4. Delta with empty content must be ignored elegantly ---
	// A delta object with no content field must not produce an event nor break
	// concatenation of surrounding content deltas.
	{
		auto t = std::make_shared<mock_transport>();
		t->stream_chunks = {
		    "data: {\"id\":\"x\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"A\"}}]}\n",
		    "data: {\"id\":\"x\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{}}]}\n",
		    "data: {\"id\":\"x\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"B\"}}]}\n",
		    "data: [DONE]\n",
		};
		llm_client client(t, "gpt-4o", api_type::openai);
		message usr;
		usr.role = "user";
		usr.content = "q";
		std::vector<message> msgs{usr};
		auto resp = client.send_chat(msgs);
		assert(resp.msg.content == "AB");
		std::cout << "  [4] empty-content delta OK" << std::endl;
	}

	// --- 5. Transport failure surfaces an error event ---
	// When post_stream() fails, the connection emits an error stream_event which
	// send_chat() folds into the response content.
	{
		auto t = std::make_shared<mock_transport>();
		t->stream_success = false;
		llm_client client(t, "gpt-4o", api_type::openai);
		message usr;
		usr.role = "user";
		usr.content = "q";
		std::vector<message> msgs{usr};
		auto resp = client.send_chat(msgs);
		assert(resp.msg.content.find("Error: Streaming request failed") != std::string::npos);
		assert(resp.msg.content.find("mock://") != std::string::npos);
		std::cout << "  [5] transport-failure error event OK" << std::endl;
	}

	// --- 6. cancel() ---
	// With no connection yet, cancel() must be a safe no-op. After a stream creates
	// the connection, cancel() must close it (which routes to transport_->cancel()).
	{
		auto t = std::make_shared<mock_transport>();
		llm_client client(t, "gpt-4o", api_type::openai);

		client.cancel(); // no connection yet -> no-op, no crash
		assert(!t->cancel_called);

		t->stream_chunks = {
		    "data: {\"id\":\"c\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"ok\"}}]}\n",
		    "data: [DONE]\n",
		};
		message usr;
		usr.role = "user";
		usr.content = "q";
		std::vector<message> msgs{usr};
		client.send_chat(msgs);
		assert(!t->cancel_called);

		client.cancel();
		assert(t->cancel_called);
		std::cout << "  [6] cancel() closes underlying connection OK" << std::endl;
	}

	// --- 7. Constructor / api_type routing: copilot endpoint ---
	// A copilot-type client routes the stream to "/chat/completions".
	{
		auto t = std::make_shared<mock_transport>();
		t->stream_chunks = {
		    "data: {\"id\":\"c\",\"object\":\"chat.completion.chunk\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"ok\"}}]}\n",
		    "data: [DONE]\n",
		};
		llm_client client(t, "gpt-4", api_type::copilot);
		message sys;
		sys.role = "system";
		sys.content = "be brief";
		message usr;
		usr.role = "user";
		usr.content = "q";
		std::vector<message> msgs{sys, usr};
		auto resp = client.send_chat(msgs);
		assert(resp.msg.content == "ok");
		assert(t->last_path == "/chat/completions");
		std::cout << "  [7] copilot api_type routing OK" << std::endl;
	}

	// --- 8. compact_response(): unsupported connection error path ---
	// An OpenAI-completion connection does not support compaction; the base
	// Connection::compact_response() returns "" and reports the error message.
	{
		auto t = std::make_shared<mock_transport>();
		llm_client client(t, "gpt-4o", api_type::openai);
		std::string err;
		auto r = client.compact_response("resp_123", &err);
		assert(r.empty());
		assert(err.find("Compaction not supported") != std::string::npos);
		std::cout << "  [8] compact_response unsupported path OK" << std::endl;
	}

	// --- 9. compact_response(): supported (Responses API) success path ---
	// An openai_response connection supports compaction; a 200 reply with a new
	// response id must be returned.
	{
		auto t = std::make_shared<mock_transport>();
		t->mock_res.status_code = 200;
		t->mock_res.body = R"({"id":"compacted_new_id"})";
		llm_client client(t, "gpt-4o", api_type::openai_response);
		std::string err;
		auto r = client.compact_response("resp_123", &err);
		assert(r == "compacted_new_id");
		assert(err.empty());
		assert(t->last_path == "/v1/responses/compact");
		std::cout << "  [9] compact_response supported success path OK" << std::endl;
	}

	// --- 10. compact_response(): supported (Responses API) failure path ---
	// A non-200 reply must surface the server-provided error message.
	{
		auto t = std::make_shared<mock_transport>();
		t->mock_res.status_code = 400;
		t->mock_res.body = R"({"error":{"message":"provided response id is invalid"}})";
		llm_client client(t, "gpt-4o", api_type::openai_response);
		std::string err;
		auto r = client.compact_response("resp_123", &err);
		assert(r.empty());
		assert(err.find("provided response id is invalid") != std::string::npos);
		std::cout << "  [10] compact_response supported failure path OK" << std::endl;
	}

	std::cout << "test_llm_client passed successfully!" << std::endl;
	return 0;
}
