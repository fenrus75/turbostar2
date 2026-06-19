#pragma once

#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../llm_types.h"
#include "../interactions/base.h"

namespace agentlib {

struct model_capabilities;

struct time_range {
	uint64_t start_time{0};
	uint64_t end_time{0};
};

enum class turn_type {
	system,
	user,
	model_response,
	tool_execution,
	error
};

/*

# subclasses of Turn

| subclass            | filename                                                            |
| ------------------- | ------------------------------------------------------------------- |
| system_turn         | src/agentlib/data/system_turn.h                                     |
| user_turn           | src/agentlib/data/user_turn.h                                       |
| model_response_turn | src/agentlib/data/model_response_turn.h                             |
| tool_execution_turn | src/agentlib/data/tool_execution_turn.h                             |
| error_turn          | src/agentlib/data/error_turn.h                                      |

*/

class Turn {
public:
	virtual ~Turn() = default;

	virtual std::string get_id() const = 0;
	virtual turn_type get_type() const = 0;

	virtual std::vector<message> to_messages(const model_capabilities& caps, int compaction_level) const = 0;
	virtual std::string to_markdown() const = 0;

	std::shared_ptr<agent_interaction> get_interaction() const { return interaction_; }
	void set_interaction(std::shared_ptr<agent_interaction> view) { interaction_ = view; }

	virtual void append_content(const std::string& chunk);
	std::string get_content() const { return content_; }
	void set_content(std::string content) { content_ = std::move(content); }

	virtual int estimate_token_count(int compaction_level) const = 0;
	
	time_range get_time_range() const { return range_; }
	void set_time_range(time_range range) { range_ = range; }

	long long get_sequence_number() const { return sequence_number_; }
	void set_sequence_number(long long seq) { sequence_number_ = seq; }

	virtual nlohmann::json serialize() const = 0;

protected:
	std::string content_;
	std::shared_ptr<agent_interaction> interaction_;
	time_range range_;
	nlohmann::json extra_fields_;
	long long sequence_number_{0};
};

} // namespace agentlib
