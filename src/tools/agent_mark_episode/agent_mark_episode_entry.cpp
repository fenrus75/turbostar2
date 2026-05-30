#include "agent_mark_episode.h"
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/system_message.h"

namespace tools {

agent_mark_episode_tool::agent_mark_episode_tool(agent_mark_episode_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<agentlib::interaction_system_message>("Episode Marked: " + args_.title + "\nSummary: " + args_.summary);
}

std::shared_ptr<agentlib::agent_interaction> agent_mark_episode_tool::get_interaction() const {
    return interaction_;
}

bool agent_mark_episode_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string agent_mark_episode_tool::execute(agentlib::tool_context& ctx) {
    if (ctx.active_agent) {
        ctx.active_agent->snapshot_episode(args_.title, args_.summary, args_.tags);
    }
    return "Episode marked: '" + args_.title + "' was successfully recorded and written to disk. The system has noted your transition.";
}

} // namespace tools