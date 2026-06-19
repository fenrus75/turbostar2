#include "tool_execution_turn.h"
#include <format>
#include <chrono>
#include <sstream>

namespace agentlib {

tool_execution_turn::tool_execution_turn(std::string id, std::vector<tool_result> results)
	: id_(std::move(id)), results_(std::move(results))
{
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_ = {now, now};
}

std::vector<message> tool_execution_turn::to_messages(const model_capabilities& /*caps*/, int /*compaction_level*/) const {
	std::vector<message> messages;
	for (const auto& res : results_) {
		message msg;
		msg.role = "tool";
		msg.content = res.content;
		msg.tool_call_id = res.call_id;
		msg.name = res.name;
		msg.timestamp = static_cast<long long>(range_.start_time);
		messages.push_back(msg);
	}
	return messages;
}

std::string tool_execution_turn::to_markdown() const {
	std::stringstream ss;
	ss << "#### Tool Executions:\n";
	for (const auto& res : results_) {
		ss << std::format("\n**`{}`** (ID: `{}`) - *{}*\n", 
						  res.name, res.call_id, res.is_error ? "Error" : "Success");
		ss << "```\n" << res.content << "\n```\n";
	}
	return ss.str();
}

int tool_execution_turn::estimate_token_count(int /*compaction_level*/) const {
	int tokens = 0;
	for (const auto& res : results_) {
		tokens += static_cast<int>(res.content.size() / 4);
	}
	return tokens;
}

void tool_execution_turn::add_result(tool_result result) {
	results_.push_back(std::move(result));
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_.end_time = now;
}

void tool_execution_turn::update_result_content(const std::string& call_id, const std::string& new_content) {
	for (auto& res : results_) {
		if (res.call_id == call_id) {
			res.content = new_content;
			break;
		}
	}
}

nlohmann::json tool_execution_turn::serialize() const {
	nlohmann::json j = extra_fields_;
	j["turn_type"] = "tool_execution";
	j["id"] = id_;
	j["results"] = results_;
	j["start_time"] = range_.start_time;
	j["end_time"] = range_.end_time;
	j["sequence_number"] = sequence_number_;
	return j;
}

std::shared_ptr<tool_execution_turn> tool_execution_turn::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::vector<tool_result> results;
	if (j.contains("results") && !j["results"].is_null()) {
		results = j["results"].get<std::vector<tool_result>>();
	}
	auto turn = std::make_shared<tool_execution_turn>(id, results);
	turn->range_.start_time = j.value("start_time", 0ULL);
	turn->range_.end_time = j.value("end_time", 0ULL);
	turn->set_sequence_number(j.value("sequence_number", 0LL));
	
	turn->extra_fields_ = j;
	turn->extra_fields_.erase("turn_type");
	turn->extra_fields_.erase("id");
	turn->extra_fields_.erase("results");
	turn->extra_fields_.erase("start_time");
	turn->extra_fields_.erase("end_time");
	turn->extra_fields_.erase("sequence_number");
	return turn;
}

} // namespace agentlib
