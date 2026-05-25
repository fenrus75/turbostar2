#include "agent_restore_context.h"
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/system_message.h"

namespace tools {

agent_restore_context_tool::agent_restore_context_tool(agent_restore_context_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<agentlib::interaction_system_message>("Context Paged In: " + args_.milestone_id);
}

std::shared_ptr<agentlib::agent_interaction> agent_restore_context_tool::get_interaction() const {
    return interaction_;
}

bool agent_restore_context_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string agent_restore_context_tool::execute(agentlib::tool_context& ctx) {
    if (ctx.active_agent) {
        if (ctx.active_agent->page_in_context(args_.milestone_id)) {
            return "Context successfully restored. The previous conversation history has been injected into your active memory.";
        } else {
            return "Error: Could not find or load milestone archive: " + args_.milestone_id;
        }
    }
    return "Error: No active agent context.";
}

} // namespace tools