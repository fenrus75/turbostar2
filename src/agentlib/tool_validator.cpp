#include "tool_validator.h"

namespace agentlib {

bool tool_validator::is_allowed_for_agent(const agent_properties &properties) const {
	if (properties.role == agent_role::summarizer) {
		return false;
	}
	return true;
}

} // namespace agentlib
