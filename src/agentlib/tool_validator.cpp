#include "tool_validator.h"
#include <algorithm>

namespace agentlib {

bool tool_validator::is_allowed_for_agent(const agent_properties &properties) const {
	const std::string &family = get_family();
	return std::find(properties.active_families.begin(), properties.active_families.end(), family) != properties.active_families.end();
}

} // namespace agentlib
