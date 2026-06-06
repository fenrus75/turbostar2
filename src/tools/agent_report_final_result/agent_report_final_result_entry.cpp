#include "agent_report_final_result.h"
#include "../../agentlib/ai_agent.h"

namespace tools
{

agent_report_final_result_tool::agent_report_final_result_tool(std::string result) : result_(std::move(result))
{
}

bool agent_report_final_result_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string agent_report_final_result_tool::execute(agentlib::tool_context &ctx)
{
	ctx.active_agent->set_final_result(result_);
	return "Final result reported successfully: " + result_;
}

} // namespace tools
