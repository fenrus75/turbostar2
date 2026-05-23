#include "agent_add_todo.h"
#include "../../agentlib/ai_agent.h"

namespace tools {

agent_add_todo_tool::agent_add_todo_tool(std::string text) : llm_tool_action("Adding todo: " + text), text_(std::move(text)) {}

bool agent_add_todo_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    if (text_.empty()) {
        out_error = "Execution Error: Todo text cannot be empty.";
        return false;
    }
    return true;
}

std::string agent_add_todo_tool::execute(agentlib::tool_context& ctx) {
    ctx.active_agent->add_todo(text_);
    set_success(ctx);
    return "Added todo: " + text_;
}

} // namespace tools