#include <format>
#include "../../agentlib/ai_agent.h"
#include "agent_todo_status.h"

namespace tools
{

agent_todo_status_tool::agent_todo_status_tool(agent_todo_status_args args) : args_(std::move(args))
{
}

bool agent_todo_status_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string agent_todo_status_tool::execute(agentlib::tool_context &ctx)
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

	auto todos = target_agent->get_todos();
	if (todos.empty()) {
		return std::format("No items in todo list for agent ID {}.", args_.id);
	}

	std::string res = std::format("Todo list for Agent ID {} ({}):\n", target_agent->get_id(), target_agent->get_name());
	for (const auto &item : todos) {
		res += std::format("- [{}] {}\n", item.completed ? "x" : " ", item.text);
	}

	return res;
}

} // namespace tools