#include "error_turn.h"
#include <format>
#include <chrono>

namespace agentlib {

error_turn::error_turn(std::string id, std::string error_message)
	: id_(std::move(id)), error_message_(std::move(error_message))
{
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_ = {now, now};
}

std::vector<message> error_turn::to_messages(const model_capabilities& /*caps*/, int /*compaction_level*/) const {
	// Transient errors are NOT sent to the LLM (they are only for the developer's TUI log)
	return {};
}

std::string error_turn::to_markdown() const {
	return std::format("> [!WARNING]\n> **Execution Error:** {}", error_message_);
}

int error_turn::estimate_token_count(int /*compaction_level*/) const {
	return 0;
}

nlohmann::json error_turn::serialize() const {
	nlohmann::json j = extra_fields_;
	j["turn_type"] = "error";
	j["id"] = id_;
	j["error_message"] = error_message_;
	j["start_time"] = range_.start_time;
	j["end_time"] = range_.end_time;
	j["sequence_number"] = sequence_number_;
	return j;
}

std::shared_ptr<error_turn> error_turn::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::string error_message = j.value("error_message", "");
	auto turn = std::make_shared<error_turn>(id, error_message);
	turn->range_.start_time = j.value("start_time", 0ULL);
	turn->range_.end_time = j.value("end_time", 0ULL);
	turn->set_sequence_number(j.value("sequence_number", 0LL));
	
	turn->extra_fields_ = j;
	turn->extra_fields_.erase("turn_type");
	turn->extra_fields_.erase("id");
	turn->extra_fields_.erase("error_message");
	turn->extra_fields_.erase("start_time");
	turn->extra_fields_.erase("end_time");
	turn->extra_fields_.erase("sequence_number");
	return turn;
}

} // namespace agentlib
