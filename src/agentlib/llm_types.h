#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace agentlib
{

struct function_call {
	std::string name;
	std::string arguments;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(function_call, name, arguments);

struct tool_call {
	std::string id;
	std::string type;
	function_call function;
	std::optional<std::string> signature;
};

void to_json(nlohmann::json &j, const tool_call &p);
void from_json(const nlohmann::json &j, tool_call &p);

struct message {
	std::string role;
	std::string content;
	std::optional<std::string> reasoning_content;
	std::optional<std::string> name;
	std::optional<std::string> tool_call_id;
	std::optional<std::vector<tool_call>> tool_calls;

	// Transient episode mapping fields
	std::string episode_id;
	int episode_level{-1}; // -1 if not a paged-in episode turn

	// Local DNN training / temporal metadata fields
	long long timestamp{0};	  // Unix timestamp in seconds
	long long duration_ms{0}; // Duration in milliseconds
};

void to_json(nlohmann::json &j, const message &p);
void from_json(const nlohmann::json &j, message &p);

struct llm_usage {
	int prompt_tokens{0};
	int completion_tokens{0};
	int total_tokens{0};
	int cached_tokens{0};
};

struct llm_chat_response {
	message msg;
	llm_usage usage;
	std::string model;
	std::string response_id;
};

struct chat_delta {
	std::string role;
	std::string content;
	std::string reasoning_content;
	std::optional<std::vector<tool_call>> tool_calls;
	bool is_final{false};
	llm_usage usage;
	std::string response_id;
};

void normalize_tool_call(tool_call &call);

} // namespace agentlib
