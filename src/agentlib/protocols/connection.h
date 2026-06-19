#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include "../llm_types.h"
#include "../agent_properties.h"

namespace agentlib {

class Conversation;

struct stream_event {
	enum class event_type {
		content_chunk,
		reasoning_chunk,
		tool_call_delta,
		completed,
		error
	};

	event_type type;
	std::string text;
	std::vector<tool_call> tool_calls;
	llm_usage usage;
	std::string response_id;
};

/*

# subclasses of Connection

| subclass                     | filename                                                    |
| ---------------------------- | ----------------------------------------------------------- |
| openai_completion_connection | src/agentlib/protocols/openai_completion_connection.h       |
| openai_response_connection   | src/agentlib/protocols/openai_response_connection.h         |
| gemini_connection            | src/agentlib/protocols/gemini_connection.h                  |
| claude_connection            | src/agentlib/protocols/claude_connection.h                  |

*/

class Connection {
public:
	virtual ~Connection() = default;

	virtual void initialize() = 0;
	virtual void close() = 0;
	virtual void sync_history(Conversation& convo) = 0;
	virtual void send_prompt(
		Conversation& convo,
		const agent_properties& properties,
		const std::vector<std::string>& active_families,
		std::function<void(const stream_event&)> callback
	) = 0;

	virtual bool supports_compaction() const { return false; }
	virtual std::string compact_response(const std::string& previous_response_id, std::string* error_msg) {
		if (error_msg) {
			*error_msg = "Compaction not supported by this connection type.";
		}
		return "";
	}

	virtual nlohmann::json serialize_state() const { return nlohmann::json::object(); }
	virtual void deserialize_state(const nlohmann::json& /*state*/) {}
};

} // namespace agentlib
