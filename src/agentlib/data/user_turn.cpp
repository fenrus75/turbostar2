#include "user_turn.h"
#include <format>
#include <chrono>

namespace agentlib {

user_turn::user_turn(std::string id, std::string content, std::optional<std::string> name)
	: id_(std::move(id)), name_(std::move(name))
{
	content_ = std::move(content);
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_ = {now, now};
}

std::vector<message> user_turn::to_messages(const model_capabilities& /*caps*/, int /*compaction_level*/) const {
	message msg;
	msg.role = "user";
	msg.content = content_;
	if (name_) {
		msg.name = *name_;
	}
	msg.timestamp = static_cast<long long>(range_.start_time);
	return {msg};
}

std::string user_turn::to_markdown() const {
	return std::format("### User\n{}", content_);
}

int user_turn::estimate_token_count(int /*compaction_level*/) const {
	return static_cast<int>(content_.size() / 4);
}

nlohmann::json user_turn::serialize() const {
	nlohmann::json j = extra_fields_;
	j["turn_type"] = "user";
	j["id"] = id_;
	j["content"] = content_;
	if (name_) {
		j["name"] = *name_;
	}
	j["start_time"] = range_.start_time;
	j["end_time"] = range_.end_time;
	j["sequence_number"] = sequence_number_;
	return j;
}

std::shared_ptr<user_turn> user_turn::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::string content = j.value("content", "");
	std::optional<std::string> name;
	if (j.contains("name") && !j["name"].is_null()) {
		name = j["name"].get<std::string>();
	}
	auto turn = std::make_shared<user_turn>(id, content, name);
	turn->range_.start_time = j.value("start_time", 0ULL);
	turn->range_.end_time = j.value("end_time", 0ULL);
	turn->set_sequence_number(j.value("sequence_number", 0LL));
	
	turn->extra_fields_ = j;
	turn->extra_fields_.erase("turn_type");
	turn->extra_fields_.erase("id");
	turn->extra_fields_.erase("content");
	turn->extra_fields_.erase("name");
	turn->extra_fields_.erase("start_time");
	turn->extra_fields_.erase("end_time");
	turn->extra_fields_.erase("sequence_number");
	return turn;
}

} // namespace agentlib
