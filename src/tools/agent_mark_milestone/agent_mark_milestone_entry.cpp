#include "agent_mark_milestone.h"
#include "../../agentlib/interactions/system_message.h"

namespace tools {

agent_mark_milestone_tool::agent_mark_milestone_tool(agent_mark_milestone_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<agentlib::interaction_system_message>("Milestone Marked: " + args_.title + "\nSummary: " + args_.summary);
}

std::shared_ptr<agentlib::agent_interaction> agent_mark_milestone_tool::get_interaction() const {
    return interaction_;
}

bool agent_mark_milestone_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string agent_mark_milestone_tool::execute(agentlib::tool_context& /*ctx*/) {
    return "Milestone successfully recorded. The system has noted your transition.";
}

} // namespace tools