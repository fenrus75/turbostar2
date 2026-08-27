#include "agent_mark_episode.h"
#include "agentlib/ai_agent.h"
#include "agentlib/interactions/system_message.h"
#include "fs_utils.h"

namespace tools {

agent_mark_episode_tool::agent_mark_episode_tool(agent_mark_episode_args args) : args_(std::move(args)) {
    std::string msg = "Episode Marked: " + args_.title;
    if (args_.title_truncated) {
        msg += " (truncated)";
    }
    msg += "\nSummary: " + args_.summary;
    if (args_.summary_truncated) {
        msg += " (truncated)";
    }
    interaction_ = std::make_shared<agentlib::interaction_system_message>(msg);
}

std::shared_ptr<agentlib::agent_interaction> agent_mark_episode_tool::get_interaction() const {
    return interaction_;
}

bool agent_mark_episode_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string agent_mark_episode_tool::execute(agentlib::tool_context& ctx) {
    std::string res = "Episode marked: '" + args_.title + "' was successfully recorded and written to disk. The system has noted your transition.";
    if (args_.title_truncated || args_.summary_truncated) {
        std::string fields;
        if (args_.title_truncated && args_.summary_truncated) {
            fields = "both title and summary were";
        } else if (args_.title_truncated) {
            fields = "title was";
        } else {
            fields = "summary was";
        }
        res += " (Note: " + fields + " truncated to 500 characters)";
    }
    if (ctx.active_agent) {
        ctx.active_agent->snapshot_episode(args_.title, args_.summary, args_.tags);
    }
    return fs_utils::wrap_prompt_untrusted_data_tag("agent_mark_episode_result", res);
}

} // namespace tools