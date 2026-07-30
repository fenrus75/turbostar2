#include "kill_subagent.h"
#include "agentlib/ai_agent.h"
#include <format>
#include <memory>

namespace tools {

kill_subagent_tool::kill_subagent_tool(kill_subagent_args args) : args_(std::move(args)) {}

bool kill_subagent_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    if (ctx.active_agent->is_read_only()) {
        out_error = "Execution Error: Agent is in read-only mode.";
        return false;
    }
    return true;
}

std::string kill_subagent_tool::execute(agentlib::tool_context &ctx)
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
		return std::format("Error: Could not find subagent with ID {}", args_.id);
	}

	const std::string name = target_agent->get_name();
	target_agent->close();
	ctx.active_agent->remove_subagent(args_.id);

	return std::format("Subagent ID {} ({}) killed successfully.", args_.id, name);
}

} // namespace tools