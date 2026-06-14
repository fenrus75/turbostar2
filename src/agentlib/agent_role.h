#pragma once
#include <string>

namespace agentlib
{

enum class agent_role {
	developer,
	summarizer,
	reviewer,
	verifier
};

inline std::string agent_role_to_string(agent_role role)
{
	switch (role) {
		case agent_role::developer:
			return "developer";
		case agent_role::summarizer:
			return "summarizer";
		case agent_role::reviewer:
			return "reviewer";
		case agent_role::verifier:
			return "verifier";
	}
	return "unknown";
}

} // namespace agentlib
