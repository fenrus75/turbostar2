#include "../../agentlib/ai_agent.h"
#include "agent_delete_todo.h"

namespace tools
{

agent_delete_todo_tool::agent_delete_todo_tool(std::string text_match)
    : llm_tool_action("Deleting todo matching: " + text_match), text_match_(std::move(text_match))
{
}

bool agent_delete_todo_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (text_match_.empty()) {
		out_error = "Execution Error: Search text cannot be empty.";
		return false;
	}
	return true;
}

std::string agent_delete_todo_tool::execute(agentlib::tool_context &ctx)
{
	std::string err;
	if (ctx.active_agent->delete_todo(text_match_, err)) {
		set_success(ctx);
		return "Task deleted.";
	}
	set_failure(ctx, err);
	return "Error: " + err;
}

} // namespace tools