#include "recording_transport.h"
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace agentlib
{

recording_transport::recording_transport(std::shared_ptr<llm_transport> inner, const std::string &log_file)
    : inner_(std::move(inner)), log_file_(log_file)
{
}

transport_response recording_transport::post(const std::string &path, const std::string &json_body)
{
	// 1. Send via inner transport (real network)
	auto res = inner_->post(path, json_body);

	// 2. Log to file in a structured JSON format
	append_to_log(path, json_body, res);
	return res;
}

bool recording_transport::post_stream(const std::string &path, const std::string &json_body,
				      std::function<bool(const char *data, size_t len, size_t off, size_t total)> callback)
{
	// For now, we don't record streaming responses easily in the same format.
	// We'll just pass through to the inner transport.
	return inner_->post_stream(path, json_body, callback);
}

void recording_transport::append_to_log(const std::string &path, const std::string &request_body, const transport_response &res)
{
	json log_array = json::array();

	// Try to read existing log
	std::ifstream in(log_file_);
	if (in.is_open()) {
		try {
			in >> log_array;
			if (!log_array.is_array()) {
				log_array = json::array();
			}
		} catch (...) {
			// Ignore parse errors, start fresh
		}
		in.close();
	}

	auto now = std::chrono::system_clock::now();
	auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

	json interaction = {{"timestamp", timestamp},
			    {"path", path},
			    {"request", json::parse(request_body, nullptr, false)}, // Parse if valid JSON, else store as string?
			    {"response", {{"status_code", res.status_code}, {"body", json::parse(res.body, nullptr, false)}}}};

	// Fallback if not valid JSON
	if (interaction["request"].is_discarded())
		interaction["request"] = request_body;
	if (interaction["response"]["body"].is_discarded())
		interaction["response"]["body"] = res.body;

	log_array.push_back(interaction);

	std::ofstream out(log_file_);
	if (out.is_open()) {
		out << log_array.dump(2);
	}
}

} // namespace agentlib
