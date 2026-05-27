#include "exit_plan_mode.h"
#include "../../agentlib/ai_agent.h"

namespace tools
{

exit_plan_mode_tool::exit_plan_mode_tool(exit_plan_mode_args args) : args_(std::move(args))
{
}

std::shared_ptr<agentlib::agent_interaction> exit_plan_mode_tool::get_interaction() const
{
    return interaction_;
}

bool exit_plan_mode_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const
{
    if (!ctx.active_agent) {
        out_error = "Error: No active agent available.";
        return false;
    }
    return true;
}

std::string exit_plan_mode_tool::execute(agentlib::tool_context& ctx)
{
    if (!ctx.active_agent->is_planning()) {
        return "Error: You are not currently in Plan Mode.";
    }

    if (!ctx.queue) {
        return "Error: No event queue available to prompt the user.";
    }

    auto promise = std::make_shared<std::promise<std::string>>();
    auto future = promise->get_future();

    editor_event ev;
    ev.type = event_type::approve_plan;
    // Combine title and summary
    ev.payload = args_.plan_title + "\n\n" + args_.plan_summary;
    ev.prompt_promise = promise;

    ctx.queue->push(ev);

    // Wait for the UI thread to resolve the promise
    std::string response;
    try {
        response = future.get();
    } catch (const std::exception &e) {
        return std::string("Error: Failed to get user response - ") + e.what();
    }

    if (response == "Approved") {
        size_t start_idx = ctx.active_agent->get_planning_start_index();
        ctx.active_agent->set_planning(false);
        if (args_.page_out_history) {
            ctx.active_agent->page_out_context(start_idx, ctx.active_agent->get_conversation().size(), args_.plan_title, args_.plan_summary, {"plan", "exploration"});
            return "Plan Mode exited. Exploratory history paged out. Modifying tools unlocked. Please execute the plan.";
        }
        return "Plan Mode exited. Modifying tools unlocked. Please execute the plan.";
    } else {
        return "Plan Rejected. User feedback:\n" + response + "\n\nPlease revise your plan and call exit_plan_mode again.";
    }
}

} // namespace tools
