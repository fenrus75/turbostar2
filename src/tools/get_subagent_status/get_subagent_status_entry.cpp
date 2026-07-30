#include <format>
#include "get_subagent_status.h"
#include "agentlib/ai_agent.h"

namespace tools {

get_subagent_status_tool::get_subagent_status_tool(get_subagent_status_args args) : args_(std::move(args)) {}

bool get_subagent_status_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string get_subagent_status_tool::execute(agentlib::tool_context& ctx) {
	auto subagents = ctx.active_agent->get_subagents();

	std::shared_ptr<agentlib::ai_agent> target_agent = nullptr;
	for (const auto &sub : subagents) {
		if (sub->get_id() == args_.id) {
			target_agent = sub;
			break;
		}
	}

	if (!target_agent) {
		return std::format("Error: Could not find subagent with ID {}", args_.id);
	}

	std::string status_str = agentlib::agent_status_to_name(target_agent->get_status());
	return std::format("Agent ID: {}\nName: {}\nStatus: {}\n", target_agent->get_id(), target_agent->get_name(), status_str);
}

} // namespace tools