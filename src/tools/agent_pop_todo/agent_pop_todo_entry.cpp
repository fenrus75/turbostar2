#include "agent_pop_todo.h"
#include "../../agentlib/ai_agent.h"

namespace tools
{

bool agent_pop_todo_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "No active agent context.";
		return false;
	}
	return true;
}

std::string agent_pop_todo_tool::execute(agentlib::tool_context &ctx)
{
	auto res = ctx.active_agent->pop_todo();
	if (res) {
		return "Popped todo item: " + *res;
	}
	return "Todo list is empty.";
}

} // namespace tools
