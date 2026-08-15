#include <chrono>
#include <thread>
#include "wait_for_subagent.h"
#include "agentlib/ai_agent.h"
#include "fs_utils.h"

namespace tools {

wait_for_subagent_tool::wait_for_subagent_tool(wait_for_subagent_args args) : args_(std::move(args)) {}

bool wait_for_subagent_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string wait_for_subagent_tool::execute(agentlib::tool_context& ctx) {
	auto subagents = ctx.active_agent->get_subagents();

	std::shared_ptr<agentlib::ai_agent> target_agent = nullptr;
	for (const auto &sub : subagents) {
		if (sub->get_id() == args_.id) {
			target_agent = sub;
			break;
		}
	}

	if (!target_agent) {
		return fs_utils::wrap_prompt_untrusted_data_tag("wait_for_subagent_result", "Error: Could not find subagent with ID " + std::to_string(args_.id));
	}

	ctx.active_agent->set_status(agentlib::agent_status::waiting, target_agent->get_id());
	target_agent->wait_until_idle();
	ctx.active_agent->set_status(agentlib::agent_status::tool_execution);

	std::string base_msg = "Subagent reached idle state successfully.";
	if (target_agent->get_status() == agentlib::agent_status::error) {
		base_msg = "Subagent finished with an error state.";
	}

	std::string result_desc = target_agent->has_final_result() ? "final result." : "interaction history.";
	std::string res = base_msg + " Use agent_get_output(" + std::to_string(args_.id) + ") to retrieve its " + result_desc;
	return fs_utils::wrap_prompt_untrusted_data_tag("wait_for_subagent_result", res);
}

} // namespace tools