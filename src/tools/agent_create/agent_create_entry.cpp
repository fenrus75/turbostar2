#include "agent_create.h"
#include "../../agentlib/ai_agent.h"
#include <sstream>

namespace tools {

agent_create_tool::agent_create_tool(agent_create_args args) : args_(std::move(args)) {}

bool agent_create_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string agent_create_tool::execute(agentlib::tool_context& ctx) {
    auto new_agent = ctx.active_agent->spawn_subagent(args_.name);
    if (!new_agent) {
        return "Error: Failed to create subagent.";
    }

    // Submit the initial profile/prompt to start the agent's work
    new_agent->submit_prompt(args_.profile);

    return "Agent '" + args_.name + "' created successfully with ID: " + std::to_string(new_agent->get_id()) + ". Agent started asynchronously. Use wait_for_agent(" + std::to_string(new_agent->get_id()) + ") to wait for the agent to finish.";
}

} // namespace tools