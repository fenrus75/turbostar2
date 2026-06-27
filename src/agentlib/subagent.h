#pragma once
#include <string>
#include <vector>
#include <optional>

namespace agentlib
{

struct subagent {
	std::string name;
	std::string description;
	std::string system_prompt;
	std::optional<std::string> model;
	std::vector<std::string> tools;
	std::vector<std::string> tool_families;
	bool read_only{false};
	std::optional<std::string> permission_mode;
	std::optional<std::string> effort;
	std::optional<int> max_turns;
	std::string file_path;
};

} // namespace agentlib
