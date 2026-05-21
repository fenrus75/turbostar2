#include "message_agent.h"
#include "../../agentlib/ai_agent.h"

namespace tools {

message_agent_tool::message_agent_tool(message_agent_args args) : args_(std::move(args)) {}

bool message_agent_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string message_agent_tool::execute(agentlib::tool_context& ctx) {
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

    target_agent->submit_prompt(args_.message);
    
    return "Message successfully queued for delivery to Agent ID " + std::to_string(args_.id) + " (" + target_agent->get_name() + ").";
}

} // namespace tools