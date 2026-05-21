#include "agent_get_output.h"
#include "../../agentlib/ai_agent.h"
#include <sstream>

namespace tools {

agent_get_output_tool::agent_get_output_tool(agent_get_output_args args) : args_(std::move(args)) {}

bool agent_get_output_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string agent_get_output_tool::execute(agentlib::tool_context& ctx) {
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

    auto interactions = target_agent->get_interactions();
    if (interactions.empty()) {
        return "Agent ID " + std::to_string(args_.id) + " has no interaction history.";
    }

    std::ostringstream oss;
    oss << "Interaction history for Agent ID " << target_agent->get_id() << " (" << target_agent->get_name() << "):\n\n";
    
    for (const auto& interaction : interactions) {
        oss << interaction->get_raw_text() << "\n\n";
    }

    oss << "--- End of History ---\n";
    oss << "If you are finished with this subagent, you should use the end_agent(" << target_agent->get_id() 
        << ") tool to clean it up and free resources. Alternatively, you can send it new instructions using the message_agent(" 
        << target_agent->get_id() << ", \"<message>\") tool.";

    return oss.str();
}

} // namespace tools