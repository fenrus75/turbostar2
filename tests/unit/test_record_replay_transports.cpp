// tests/unit/test_record_replay_transports.cpp
//
// Unit tests for the agentlib recording / replay transport pair.
//
// Source files covered:
//   - src/agentlib/replay_transport.cpp
//   - src/agentlib/recording_transport.cpp
//
// Methods exercised:
//   replay_transport:
//     - replay_transport(const std::string &playback_file)  // valid / missing / malformed file ctor
//     - post(path, json_body)            // sequential playback; past-end -> 404 + error body + last_error
//     - post_stream(path, body, callback) // single-chunk body delivery on 200; false on 404
//     - get_base_url(), get_last_error()
//     - detect_api_type()                // first log entry "path" substring: responses/gemini/copilot/else
//   recording_transport:
//     - recording_transport(inner, log_file)
//     - post(path, json_body)            // forwards to inner AND appends an entry to the log file
//     - post_stream(path, body, callback) // pass-through to inner (streaming is not logged)
//     - append_to_log() (private)        // verified indirectly via a record -> replay round-trip
//
// Test data strategy:
//   All playback / log JSON files are generated at runtime into a per-process
//   temporary directory (test_watchdog::scoped_test_home "record_replay"), so
//   this test is fully self-contained and does not rely on committed fixtures.
//
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "agentlib/recording_transport.h"
#include "agentlib/replay_transport.h"
#include "test_watchdog.h"

using namespace agentlib;
using json = nlohmann::json;

// Absolute directory where all runtime-generated traffic files are written.
static std::string g_traffic_dir;

static void write_file(const std::string &path, const std::string &content)
{
	std::ofstream out(path);
	assert(out.is_open());
	out << content;
}

static std::string write_json_file(const std::string &name, const json &doc)
{
	const std::string full = (std::filesystem::path(g_traffic_dir) / name).string();
	write_file(full, doc.dump(2));
	return full;
}

// Builds a single playback/log entry matching the exact schema written by
// recording_transport::append_to_log:
//   { "timestamp": <number>, "path": <string>, "request": <json>,
//     "response": { "status_code": <int>, "body": <string-or-object> } }
static json mk_entry(long ts, const std::string &path, const json &request, int code, const json &body)
{
	return {{"timestamp", ts}, {"path", path}, {"request", request}, {"response", {{"status_code", code}, {"body", body}}}};
}

// Fake inner transport used to verify recording_transport forwarding behaviour.
class mock_transport : public llm_transport
{
      public:
	std::vector<transport_response> responses; // canned replies consumed by post()
	size_t call_count = 0;
	std::string last_path;
	std::string last_body;
	bool stream_result = true;

	transport_response post(const std::string &path, const std::string &json_body) override
	{
		last_path = path;
		last_body = json_body;
		if (call_count < responses.size()) {
			return responses[call_count++];
		}
		return {404, "{}"};
	}

	bool post_stream(const std::string &path, const std::string &json_body,
			 std::function<bool(const char *, size_t, size_t, size_t)> cb) override
	{
		(void)cb; // streaming is fake: we only verify forwarding
		last_path = path;
		last_body = json_body;
		return stream_result;
	}

	std::string get_base_url() const override
	{
		return "mock://inner";
	}
	std::string get_last_error() const override
	{
		return "";
	}
};

// replay_transport with a valid playback file: sequential order, past-end 404.
void test_replay_valid_playback()
{
	std::cout << "  [replay] valid playback file" << std::endl;

	// response[0] body is a JSON document stored as a string (verbatim on replay).
	// response[1] body is a JSON object (re-serialized on replay).
	json msg = {{"role", "assistant"}, {"content", "Second reply"}};
	json choice = {{"message", msg}};
	json body2 = {{"choices", json::array({choice})}};

	json arr = json::array();
	arr.push_back(mk_entry(1000, "/v1/chat/completions", json::parse(R"({"messages":[]})"), 200,
			       R"({"choices":[{"message":{"role":"assistant","content":"First reply"}}]})"));
	arr.push_back(mk_entry(1001, "/v1/chat/completions", json::parse(R"({"messages":[]})"), 200, body2));
	const std::string file = write_json_file("replay_valid.json", arr);

	replay_transport rep(file);
	assert(rep.get_base_url() == "replay://" + file);

	// First post() replays response[0]: string-typed body is returned verbatim.
	transport_response r0 = rep.post("/v1/chat/completions", "{}");
	assert(r0.status_code == 200);
	assert(r0.body == R"({"choices":[{"message":{"role":"assistant","content":"First reply"}}]})");
	assert(rep.get_last_error().empty());

	// Second post() replays response[1]: object-typed body is re-serialized.
	transport_response r1 = rep.post("/v1/chat/completions", "{}");
	assert(r1.status_code == 200);
	assert(json::parse(r1.body)["choices"][0]["message"]["content"] == "Second reply");
	assert(rep.get_last_error().empty());

	// Past the end: 404 + error body + last_error set.
	transport_response r2 = rep.post("/v1/chat/completions", "{}");
	assert(r2.status_code == 404);
	assert(r2.body == "{\"error\": \"End of playback file reached\"}");
	assert(rep.get_last_error() == "End of playback file reached");
}

// replay_transport with a missing file: ctor must not throw; behaves like empty.
void test_replay_missing_file()
{
	std::cout << "  [replay] missing file" << std::endl;
	const std::string missing = (std::filesystem::path(g_traffic_dir) / "does_not_exist.json").string();

	replay_transport rep(missing); // must not throw
	// get_last_error() stays empty until a post() consumes (or exhausts) the stream.
	assert(rep.get_last_error().empty());

	transport_response res = rep.post("/v1/chat/completions", "{}");
	assert(res.status_code == 404);
	assert(res.body.find("End of playback file reached") != std::string::npos);
	assert(rep.get_last_error() == "End of playback file reached");

	// An empty log_array_ defaults to the openai API type.
	assert(rep.detect_api_type() == api_type::openai);
}

// replay_transport with malformed / non-array JSON: ctor must not throw; behaves like empty.
void test_replay_invalid_json()
{
	std::cout << "  [replay] invalid / non-array JSON" << std::endl;

	// Genuinely malformed JSON: parser throws inside the ctor, which swallows it.
	const std::string malformed = (std::filesystem::path(g_traffic_dir) / "replay_bad_syntax.json").string();
	write_file(malformed, "{ this is not valid json !!!");
	replay_transport rep_malformed(malformed); // must not throw
	assert(rep_malformed.detect_api_type() == api_type::openai);
	transport_response r = rep_malformed.post("/v1/chat/completions", "{}");
	assert(r.status_code == 404);
	assert(rep_malformed.get_last_error() == "End of playback file reached");

	// Parses fine but is not an array: ctor keeps log_array_ empty, same as missing.
	const std::string not_array = (std::filesystem::path(g_traffic_dir) / "replay_not_array.json").string();
	write_file(not_array, R"({"a": 1})");
	replay_transport rep_object(not_array); // must not throw
	transport_response r2 = rep_object.post("/v1/chat/completions", "{}");
	assert(r2.status_code == 404);
	assert(rep_object.get_last_error() == "End of playback file reached");
}

// replay_transport::post_stream: whole body in one chunk on 200, false on 404.
void test_replay_post_stream()
{
	std::cout << "  [replay] post_stream" << std::endl;

	json arr = json::array();
	arr.push_back(mk_entry(1, "/v1/chat/completions", "{}", 200, R"({"delta":"hello chunk"})"));
	const std::string file = write_json_file("replay_stream.json", arr);

	replay_transport rep(file);
	std::string received;
	size_t chunks = 0;
	size_t seen_off = 0, seen_len = 0, seen_total = 0;
	bool ok = rep.post_stream("/v1/chat/completions", "{}", [&](const char *data, size_t len, size_t off, size_t total) {
		++chunks;
		received.assign(data, len);
		seen_off = off;
		seen_len = len;
		seen_total = total;
		return true;
	});
	assert(ok);
	assert(chunks == 1); // delivered in a single callback call
	assert(received == R"({"delta":"hello chunk"})");
	assert(seen_off == 0);
	assert(seen_len == received.size());
	assert(seen_total == received.size());

	// 404 case (missing file): post_stream returns false and never invokes the callback.
	replay_transport rep_missing((std::filesystem::path(g_traffic_dir) / "nope.json").string());
	bool invoked = false;
	bool ok2 = rep_missing.post_stream("/v1/chat/completions", "{}", [&](const char *, size_t, size_t, size_t) {
		invoked = true;
		return true;
	});
	assert(!ok2);
	assert(!invoked);
}

// replay_transport::detect_api_type is driven by the FIRST log entry's "path" field.
void test_replay_detect_api_type()
{
	std::cout << "  [replay] detect_api_type from first entry 'path'" << std::endl;

	auto check = [](const char *name, const std::string &path_field, api_type expected) {
		json arr = json::array();
		arr.push_back(mk_entry(1, path_field, "{}", 200, "ok"));
		const std::string file = write_json_file(name, arr);
		replay_transport rep(file);
		assert(rep.detect_api_type() == expected);
	};

	check("detect_responses.json", "/v1/responses", api_type::openai_response);
	check("detect_gemini.json", "/v1beta/models/gemini-2.5-flash:generateContent", api_type::gemini);
	check("detect_copilot.json", "/copilot/v1/chat/completions", api_type::copilot);
	check("detect_default.json", "/v1/chat/completions", api_type::openai);
}

// recording_transport::post forwards to inner + appends to log; post_stream passes through.
void test_recording_forwards_and_logs()
{
	std::cout << "  [recording] post forwards + appends; post_stream passes through" << std::endl;

	auto inner = std::make_shared<mock_transport>();
	inner->responses = {{200, "First response"}, {200, R"({"answer":42})"}};

	const std::string log_file = (std::filesystem::path(g_traffic_dir) / "recording_log.json").string();
	recording_transport rec(inner, log_file);
	assert(rec.get_base_url() == "mock://inner");

	// post() returns the inner transport's result verbatim...
	transport_response r0 = rec.post("/v1/chat/completions", R"({"model":"x"})");
	assert(r0.status_code == 200);
	assert(r0.body == "First response");
	assert(inner->last_path == "/v1/chat/completions");
	assert(inner->last_body == R"({"model":"x"})");

	transport_response r1 = rec.post("/v1/chat/completions", R"({"model":"y"})");
	assert(r1.status_code == 200);
	assert(r1.body == R"({"answer":42})");

	// ...while post_stream passes straight through and is NOT logged.
	bool stream_ok =
	    rec.post_stream("/v1/chat/completions", R"({"stream":true})", [](const char *, size_t, size_t, size_t) { return true; });
	assert(stream_ok);
	assert(inner->last_path == "/v1/chat/completions");
	assert(inner->last_body == R"({"stream":true})");

	// Verify the recorded log JSON structure: an array holding exactly the 2 post()
	// interactions (the stream call must not appear), each with timestamp/path/request/
	// response.status_code/response.body.
	std::ifstream in(log_file);
	assert(in.is_open());
	json recorded;
	in >> recorded;
	assert(recorded.is_array());
	assert(recorded.size() == 2);

	assert(recorded[0]["timestamp"].is_number());
	assert(recorded[0]["path"] == "/v1/chat/completions");
	// Valid-JSON request bodies are stored as objects.
	assert(recorded[0]["request"]["model"] == "x");
	assert(recorded[0]["response"]["status_code"] == 200);
	// Non-JSON response bodies are stored as plain strings.
	assert(recorded[0]["response"]["body"] == "First response");

	assert(recorded[1]["timestamp"].is_number());
	assert(recorded[1]["path"] == "/v1/chat/completions");
	assert(recorded[1]["request"]["model"] == "y");
	assert(recorded[1]["response"]["status_code"] == 200);
	// Valid-JSON response bodies are stored as objects.
	assert(recorded[1]["response"]["body"]["answer"] == 42);
}

// Round-trip: record two interactions with recording_transport, then replay the
// same log file with replay_transport and check the responses come back in order.
// This verifies append_to_log() indirectly.
void test_recording_roundtrip()
{
	std::cout << "  [recording] round-trip: record then replay" << std::endl;

	auto inner = std::make_shared<mock_transport>();
	inner->responses = {{200, "First response"}, {201, R"({"answer":42})"}};

	const std::string log_file = (std::filesystem::path(g_traffic_dir) / "recording_roundtrip.json").string();
	{
		recording_transport rec(inner, log_file);
		rec.post("/v1/chat/completions", R"({"prompt":"one"})");
		rec.post("/v1/chat/completions", R"({"prompt":"two"})");
	}

	replay_transport rep(log_file);

	transport_response r0 = rep.post("/v1/chat/completions", "{}");
	assert(r0.status_code == 200);
	assert(r0.body == "First response");

	transport_response r1 = rep.post("/v1/chat/completions", "{}");
	assert(r1.status_code == 201);
	assert(json::parse(r1.body)["answer"] == 42);

	// The recorded file contains exactly two interactions -> third replay is 404.
	transport_response r2 = rep.post("/v1/chat/completions", "{}");
	assert(r2.status_code == 404);
	assert(rep.get_last_error() == "End of playback file reached");
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_watchdog::scoped_test_home guard("record_replay");

	g_traffic_dir = (std::filesystem::path(guard.get_path()) / "traffic").string();
	std::filesystem::create_directories(g_traffic_dir);

	std::cout << "Running record/replay transport tests..." << std::endl;
	test_replay_valid_playback();
	test_replay_missing_file();
	test_replay_invalid_json();
	test_replay_post_stream();
	test_replay_detect_api_type();
	test_recording_forwards_and_logs();
	test_recording_roundtrip();

	// Clean up all generated traffic files.
	std::filesystem::remove_all(guard.get_path());

	std::cout << "All record/replay transport tests passed." << std::endl;
	return 0;
}
