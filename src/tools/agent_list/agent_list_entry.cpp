#include "agentlib/ai_agent.h"
#include "agent_list.h"
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace tools
{

/**
 * @brief Validates runtime context.
 */
bool agent_list_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

/**
 * @brief Executes listing of active subagents.
 */
std::string agent_list_tool::execute(agentlib::tool_context &ctx)
{
	auto subagents = ctx.active_agent->get_subagents();
	if (subagents.empty()) {
		return "| ID | Name | Status |\n|---|---|---|\n";
	}

	std::string table = "| ID | Name | Status |\n|---|---|---|\n";

	for (const auto &sub : subagents) {
		std::string status_str;
		switch (sub->get_status()) {
			case agentlib::agent_status::idle:
				status_str = "Idle";
				break;
			case agentlib::agent_status::thinking:
				status_str = "Thinking";
				break;
			case agentlib::agent_status::tool_execution:
				status_str = "Tool Execution";
				break;
			case agentlib::agent_status::error:
				status_str = "Error";
				break;
			case agentlib::agent_status::waiting:
				status_str = "Waiting";
				break;
			default:
				status_str = "Unknown";
				break;
		}
		table += std::format("| {} | {} | {} |\n", sub->get_id(), sub->get_name(), status_str);
	}
	return table;
}

} // namespace tools