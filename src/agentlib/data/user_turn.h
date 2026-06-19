#pragma once

#include "turn.h"

namespace agentlib {

class user_turn : public Turn {
public:
	user_turn(std::string id, std::string content, std::optional<std::string> name = std::nullopt);

	std::string get_id() const override { return id_; }
	turn_type get_type() const override { return turn_type::user; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const override;
	std::string to_markdown() const override;

	int estimate_token_count(int compaction_level) const override;

	nlohmann::json serialize() const override;
	static std::shared_ptr<user_turn> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::optional<std::string> name_;
};

} // namespace agentlib
