#include "agent_compress_history.h"
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/system_message.h"

namespace tools {

agent_compress_history_tool::agent_compress_history_tool(agent_compress_history_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<agentlib::interaction_system_message>("History Paged Out: " + args_.title);
}

std::shared_ptr<agentlib::agent_interaction> agent_compress_history_tool::get_interaction() const {
    return interaction_;
}

bool agent_compress_history_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string agent_compress_history_tool::execute(agentlib::tool_context& ctx) {
    if (ctx.active_agent) {
        ctx.active_agent->page_out_prior_context(args_.target_milestone_id, args_.include_all_prior, args_.title, args_.summary, args_.tags);
        return "History successfully paged out. Your context window has been cleared and replaced with a summary pointer.";
    }
    return "Error: No active agent context.";
}

} // namespace tools