#include <sstream>
#include "../../agentlib/ai_agent.h"
#include "agent_status.h"

namespace tools
{

agent_status_tool::agent_status_tool(agent_status_args args) : args_(std::move(args))
{
}

bool agent_status_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string agent_status_tool::execute(agentlib::tool_context &ctx)
{
	auto subagents = ctx.active_agent->get_subagents();

	std::shared_ptr<agentlib::ai_agent> target_agent = nullptr;
	for (const auto &sub : subagents) {
		if (sub->get_id() == args_.id) {
			target_agent = sub;
			break;
		}
	}

	if (!target_agent) {
		return "Error: Could not find subagent with ID " + std::to_string(args_.id);
	}

	std::ostringstream oss;
	oss << "Agent ID: " << target_agent->get_id() << "\n";
	oss << "Name: " << target_agent->get_name() << "\n";

	std::string status_str = agentlib::agent_status_to_name(target_agent->get_status());
	oss << "Status: " << status_str << "\n";

	// We can expand this later with more details, like last error, tokens, etc.

	return oss.str();
}

} // namespace tools