#include "model_response_turn.h"
#include <format>
#include <chrono>
#include <sstream>
#include <algorithm>

namespace agentlib {

model_response_turn::model_response_turn(std::string id, std::string content, 
										 std::optional<std::string> reasoning_content,
										 std::vector<tool_call> tool_calls,
										 std::string response_id)
	: id_(std::move(id)), reasoning_content_(std::move(reasoning_content)),
	  tool_calls_(std::move(tool_calls)), response_id_(std::move(response_id))
{
	content_ = std::move(content);
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_ = {now, now};
}

std::vector<message> model_response_turn::to_messages(const model_capabilities& /*caps*/, int compaction_level) const {
	message msg;
	msg.role = "assistant";
	msg.content = content_;
	msg.reasoning_content = reasoning_content_;
	if (!tool_calls_.empty()) {
		msg.tool_calls = tool_calls_;
	}
	msg.timestamp = static_cast<long long>(range_.start_time);

	// Compaction Level 1: Strip explicit reasoning content and <think> blocks
	if (compaction_level >= 1) {
		if (msg.reasoning_content) {
			msg.reasoning_content.reset();
		}

		// Strip inline <think>...</think> tags from content
		size_t start_pos = 0;
		while ((start_pos = msg.content.find("<think>")) != std::string::npos) {
			size_t end_pos = msg.content.find("</think>", start_pos);
			if (end_pos != std::string::npos) {
				msg.content.erase(start_pos, (end_pos + 8) - start_pos);
			} else {
				msg.content.erase(start_pos);
				break;
			}
		}
		// Trim leading newlines/whitespace
		while (!msg.content.empty() && std::isspace(static_cast<unsigned char>(msg.content.front()))) {
			msg.content.erase(msg.content.begin());
		}
	}

	// Compaction Level 2: Clear assistant content when tools are called
	if (compaction_level >= 2) {
		if (msg.tool_calls && !msg.tool_calls->empty() && !msg.content.empty()) {
			msg.content.clear();
		}
	}

	return {msg};
}

std::string model_response_turn::to_markdown() const {
	std::stringstream ss;
	if (reasoning_content_ && !reasoning_content_->empty()) {
		ss << "<think>\n" << *reasoning_content_ << "\n</think>\n\n";
	}
	ss << content_;
	if (!tool_calls_.empty()) {
		ss << "\n\n*Requested Tools:*\n";
		for (const auto& tc : tool_calls_) {
			ss << std::format("* `{}` (ID: `{}`) with arguments `{}`\n", 
							  tc.function.name, tc.id, tc.function.arguments);
		}
	}
	return ss.str();
}

void model_response_turn::append_content(const std::string& chunk) {
	Turn::append_content(chunk);
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_.end_time = now;
}

void model_response_turn::append_reasoning_content(const std::string& chunk) {
	if (!reasoning_content_) {
		reasoning_content_ = "";
	}
	*reasoning_content_ += chunk;
	if (reasoning_interaction_) {
		reasoning_interaction_->push_incremental_content(chunk);
	}
	uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	range_.end_time = now;
}

int model_response_turn::estimate_token_count(int compaction_level) const {
	int tokens = 0;
	std::string text_to_measure = content_;
	std::string reasoning_to_measure = reasoning_content_ ? *reasoning_content_ : "";

	if (compaction_level >= 1) {
		reasoning_to_measure.clear();
		// Strip inline <think> tags from text_to_measure
		size_t start_pos = 0;
		while ((start_pos = text_to_measure.find("<think>")) != std::string::npos) {
			size_t end_pos = text_to_measure.find("</think>", start_pos);
			if (end_pos != std::string::npos) {
				text_to_measure.erase(start_pos, (end_pos + 8) - start_pos);
			} else {
				text_to_measure.erase(start_pos);
				break;
			}
		}
	}

	if (compaction_level >= 2 && !tool_calls_.empty()) {
		text_to_measure.clear();
	}

	tokens += static_cast<int>(text_to_measure.size() / 4);
	tokens += static_cast<int>(reasoning_to_measure.size() / 4);
	
	// Add minor overhead for tool calls metadata
	if (!tool_calls_.empty()) {
		tokens += static_cast<int>(tool_calls_.size() * 30);
	}
	return tokens;
}

nlohmann::json model_response_turn::serialize() const {
	nlohmann::json j = extra_fields_;
	j["turn_type"] = "model_response";
	j["id"] = id_;
	j["content"] = content_;
	if (reasoning_content_) {
		j["reasoning_content"] = *reasoning_content_;
	}
	if (!tool_calls_.empty()) {
		j["tool_calls"] = tool_calls_;
	}
	if (!response_id_.empty()) {
		j["response_id"] = response_id_;
	}
	j["start_time"] = range_.start_time;
	j["end_time"] = range_.end_time;
	j["sequence_number"] = sequence_number_;
	return j;
}

std::shared_ptr<model_response_turn> model_response_turn::deserialize(const nlohmann::json& j) {
	std::string id = j.at("id").get<std::string>();
	std::string content = j.value("content", "");
	std::optional<std::string> reasoning_content;
	if (j.contains("reasoning_content") && !j["reasoning_content"].is_null()) {
		reasoning_content = j["reasoning_content"].get<std::string>();
	}
	std::vector<tool_call> tool_calls;
	if (j.contains("tool_calls") && !j["tool_calls"].is_null()) {
		tool_calls = j["tool_calls"].get<std::vector<tool_call>>();
	}
	std::string response_id = j.value("response_id", "");
	
	auto turn = std::make_shared<model_response_turn>(id, content, reasoning_content, tool_calls, response_id);
	turn->range_.start_time = j.value("start_time", 0ULL);
	turn->range_.end_time = j.value("end_time", 0ULL);
	turn->set_sequence_number(j.value("sequence_number", 0LL));
	
	turn->extra_fields_ = j;
	turn->extra_fields_.erase("turn_type");
	turn->extra_fields_.erase("id");
	turn->extra_fields_.erase("content");
	turn->extra_fields_.erase("reasoning_content");
	turn->extra_fields_.erase("tool_calls");
	turn->extra_fields_.erase("response_id");
	turn->extra_fields_.erase("start_time");
	turn->extra_fields_.erase("end_time");
	turn->extra_fields_.erase("sequence_number");
	return turn;
}

} // namespace agentlib
