#pragma once

#include "turn.h"

namespace agentlib {

class model_response_turn : public Turn {
public:
	model_response_turn(std::string id, std::string content, 
						std::optional<std::string> reasoning_content = std::nullopt,
						std::vector<tool_call> tool_calls = {},
						std::string response_id = "");

	std::string get_id() const override { return id_; }
	turn_type get_type() const override { return turn_type::model_response; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const override;
	std::string to_markdown() const override;

	void append_content(const std::string& chunk) override;
	void append_reasoning_content(const std::string& chunk);

	int estimate_token_count(int compaction_level) const override;

	std::vector<tool_call> get_tool_calls() const { return tool_calls_; }
	void set_tool_calls(std::vector<tool_call> calls) { tool_calls_ = std::move(calls); }

	std::string get_response_id() const { return response_id_; }
	std::optional<std::string> get_reasoning_content() const { return reasoning_content_; }

	std::shared_ptr<agent_interaction> get_reasoning_interaction() const { return reasoning_interaction_; }
	void set_reasoning_interaction(std::shared_ptr<agent_interaction> view) { reasoning_interaction_ = view; }

	nlohmann::json serialize() const override;
	static std::shared_ptr<model_response_turn> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::optional<std::string> reasoning_content_;
	std::vector<tool_call> tool_calls_;
	std::string response_id_;
	std::shared_ptr<agent_interaction> reasoning_interaction_;
};

} // namespace agentlib
