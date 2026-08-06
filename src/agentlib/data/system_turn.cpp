#include "system_turn.h"
#include "agentlib/interactions/interactions.h"
#include <format>
#include <chrono>

namespace agentlib {

system_turn::system_turn(std::string id, std::string content, std::string purpose)
	: id_(std::move(id)), purpose_(std::move(purpose))
{
	content_ = std::move(content);
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_ = {now, now};
}

std::vector<message> system_turn::to_messages(const model_capabilities& /*caps*/, int /*compaction_level*/) const {
	message msg;
	msg.role = "system";
	msg.content = content_;
	msg.timestamp = static_cast<long long>(range_.start_time);
	return {msg};
}

std::string system_turn::to_markdown() const {
	return std::format("> [!NOTE]\n> **System Instructions ({})**\n> {}", purpose_, content_);
}

int system_turn::estimate_token_count(int /*compaction_level*/) const {
	return static_cast<int>(content_.size() / 4);
}

nlohmann::json system_turn::serialize() const {
	nlohmann::json j = extra_fields_;
	j["turn_type"] = "system";
	j["id"] = id_;
	j["content"] = content_;
	j["purpose"] = purpose_;
	j["start_time"] = range_.start_time;
	j["end_time"] = range_.end_time;
	j["sequence_number"] = sequence_number_;
	return j;
}

std::shared_ptr<system_turn> system_turn::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::string content = j.value("content", "");
	std::string purpose = j.value("purpose", "base");
	auto turn = std::make_shared<system_turn>(id, content, purpose);
	turn->range_.start_time = j.value("start_time", 0ULL);
	turn->range_.end_time = j.value("end_time", 0ULL);
	turn->set_sequence_number(j.value("sequence_number", 0LL));
	if (purpose == "ui_notification" && !content.empty()) {
		turn->set_interaction(std::make_shared<interaction_system_message>(content));
	}
	
	turn->extra_fields_ = j;
	turn->extra_fields_.erase("turn_type");
	turn->extra_fields_.erase("id");
	turn->extra_fields_.erase("content");
	turn->extra_fields_.erase("purpose");
	turn->extra_fields_.erase("start_time");
	turn->extra_fields_.erase("end_time");
	turn->extra_fields_.erase("sequence_number");
	return turn;
}

} // namespace agentlib
