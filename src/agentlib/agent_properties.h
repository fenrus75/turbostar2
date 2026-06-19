#pragma once
#include "agent_role.h"

namespace agentlib
{

struct agent_properties {
	agent_role role = agent_role::developer;
	bool read_only = false;
};

} // namespace agentlib
