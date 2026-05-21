#include "agent_todo_status.h"
#include "../../agentlib/ai_agent.h"
#include <sstream>

namespace tools {

agent_todo_status_tool::agent_todo_status_tool(agent_todo_status_args args) : args_(std::move(args)) {}

bool agent_todo_status_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string agent_todo_status_tool::execute(agentlib::tool_context& ctx) {
    auto subagents = ctx.active_agent->get_subagents();
    
    std::shared_ptr<agentlib::ai_agent> target_agent = nullptr;
    for (const auto& sub : subagents) {
        if (sub->get_id() == args_.id) {
            target_agent = sub;
            break;
        }
    }

    if (!target_agent) {
        return "Error: Could not find subagent with ID " + std::to_string(args_.id);
    }

    auto todos = target_agent->get_todos();
    if (todos.empty()) {
        return "No items in todo list for agent ID " + std::to_string(args_.id) + ".";
    }

    std::ostringstream oss;
    oss << "Todo list for Agent ID " << target_agent->get_id() << " (" << target_agent->get_name() << "):\n";
    for (const auto& item : todos) {
        if (item.completed) {
            oss << "- [x] " << item.text << "\n";
        } else {
            oss << "- [ ] " << item.text << "\n";
        }
    }

    return oss.str();
}

} // namespace tools