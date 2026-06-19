#pragma once
#include "agent_role.h"
#include <vector>
#include <string>

namespace agentlib
{

struct agent_properties {
	agent_role role = agent_role::developer;
	bool read_only = false;
	std::vector<std::string> active_families = {"base"};
};

} // namespace agentlib
