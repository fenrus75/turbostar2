#include <format>
#include "../../agentlib/ai_agent.h"
#include "agent_list_todos.h"

namespace tools
{

bool agent_list_todos_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string agent_list_todos_tool::execute(agentlib::tool_context &ctx)
{
	auto todos = ctx.active_agent->get_todos();
	if (todos.empty()) {
		set_success(ctx, "0 todos");
		return "No items in todo list.";
	}

	std::string res;
	for (const auto &item : todos) {
		res += std::format("- [{}] {}\n", item.completed ? "x" : " ", item.text);
	}
	set_success(ctx, std::format("{} todos", todos.size()));
	return res;
}

} // namespace tools