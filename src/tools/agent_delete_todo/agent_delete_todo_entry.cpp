#include "agentlib/ai_agent.h"
#include "agent_delete_todo.h"
#include <format>
#include <string>
#include <utility>

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
	if (ctx.active_agent->is_read_only()) {
		out_error = "Execution Error: Agent is in read-only mode.";
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
		return std::format("Task deleted.");
	}
	set_failure(ctx, err);
	return std::format("Error: {}", err);
}

} // namespace tools