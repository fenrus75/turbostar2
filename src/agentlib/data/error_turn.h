#pragma once

#include "turn.h"

namespace agentlib {

class error_turn : public Turn {
public:
	error_turn(std::string id, std::string error_message);

	std::string get_id() const override { return id_; }
	turn_type get_type() const override { return turn_type::error; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const override;
	std::string to_markdown() const override;

	int estimate_token_count(int compaction_level) const override;

	nlohmann::json serialize() const override;
	static std::shared_ptr<error_turn> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::string error_message_;
};

} // namespace agentlib
