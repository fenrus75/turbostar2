#pragma once

#include "turn.h"

namespace agentlib {

struct tool_result {
	std::string call_id;
	std::string name;
	std::string content;
	bool is_error{false};
};

inline void to_json(nlohmann::json &j, const tool_result &p)
{
	j = nlohmann::json{{"call_id", p.call_id}, {"name", p.name}, {"content", p.content}, {"is_error", p.is_error}};
}

inline void from_json(const nlohmann::json &j, tool_result &p)
{
	j.at("call_id").get_to(p.call_id);
	j.at("name").get_to(p.name);
	j.at("content").get_to(p.content);
	p.is_error = j.value("is_error", false);
}

class tool_execution_turn : public Turn {
public:
	tool_execution_turn(std::string id, std::vector<tool_result> results = {});

	std::string get_id() const override { return id_; }
	turn_type get_type() const override { return turn_type::tool_execution; }

	std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const override;
	std::string to_markdown() const override;

	int estimate_token_count(int compaction_level) const override;

	void add_result(tool_result result);
	const std::vector<tool_result>& get_results() const { return results_; }
	void update_result_content(const std::string& call_id, const std::string& new_content);

	void add_tool_interaction(std::shared_ptr<agent_interaction> inter) { tool_interactions_.push_back(inter); }
	const std::vector<std::shared_ptr<agent_interaction>>& get_tool_interactions() const { return tool_interactions_; }

	nlohmann::json serialize() const override;
	static std::shared_ptr<tool_execution_turn> deserialize(const nlohmann::json& j);

private:
	std::string id_;
	std::vector<tool_result> results_;
	std::vector<std::shared_ptr<agent_interaction>> tool_interactions_;
};

} // namespace agentlib
