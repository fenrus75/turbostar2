#pragma once
#include <string>

namespace agentlib
{

struct tool_family
{
	std::string name;
	std::string description;
	bool is_active{false};
	bool is_system{false};
};

} // namespace agentlib
