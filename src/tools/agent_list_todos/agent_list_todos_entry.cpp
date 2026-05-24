#include <sstream>
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

	std::ostringstream oss;
	size_t count = 0;
	for (const auto &item : todos) {
		if (item.completed) {
			oss << "- [x] " << item.text << "\n";
		} else {
			oss << "- [ ] " << item.text << "\n";
		}
		count++;
	}
	set_success(ctx, std::to_string(count) + " todos");
	return oss.str();
}

} // namespace tools