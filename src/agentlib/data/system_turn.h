#pragma once

#include "turn.h"

namespace agentlib {

class system_turn : public Turn {
public:
	system_turn(std::string id, std::string content, std::string purpose);

	std::string get_id() const override { return id_; }
	turn_type get_type() const override { return turn_type::system; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const override;
	std::string to_markdown() const override;

	int estimate_token_count(int compaction_level) const override;

	nlohmann::json serialize() const override;
	static std::shared_ptr<system_turn> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::string purpose_;
};

} // namespace agentlib
