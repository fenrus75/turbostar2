#include "enter_plan_mode.h"
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/system_message.h"

namespace tools
{

enter_plan_mode_tool::enter_plan_mode_tool(enter_plan_mode_args args) : args_(std::move(args))
{
    interaction_ = std::make_shared<agentlib::interaction_system_message>("Switching to Plan Mode" + (args_.reason.empty() ? "" : ": " + args_.reason));
}

std::shared_ptr<agentlib::agent_interaction> enter_plan_mode_tool::get_interaction() const
{
    return interaction_;
}

bool enter_plan_mode_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const
{
    if (!ctx.active_agent) {
        out_error = "Error: No active agent available.";
        return false;
    }
    return true;
}

std::string enter_plan_mode_tool::execute(agentlib::tool_context& ctx)
{
    if (ctx.active_agent->is_planning()) {
        return "You are already in Plan Mode.";
    }

    size_t convo_size = ctx.active_agent->get_conversation().size();
    size_t current_index = convo_size > 0 ? convo_size - 1 : 0;
    ctx.active_agent->set_planning(true, current_index);

    std::string plan_file = args_.plan_file;
    if (plan_file.empty()) {
        plan_file = "docs/plan.md";
    }
    ctx.active_agent->set_plan_file(plan_file);

    return "Plan Mode entered. You may now explore the codebase using read-only tools to formulate a plan.\n"
           "You are ALSO allowed to use file writing tools (e.g. fs_write_file, fs_replace_lines) ONLY on the designated plan file: " + plan_file + "\n"
           "Use this file to draft, detail, and refine your plan.";
}

} // namespace tools
