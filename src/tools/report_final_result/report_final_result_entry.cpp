#include "report_final_result.h"
#include "agentlib/ai_agent.h"
#include "fs_utils.h"

namespace tools {

report_final_result_tool::report_final_result_tool(report_final_result_args args) : args_(std::move(args)) {}

bool report_final_result_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (!ctx.active_agent) {
        out_error = "Execution Error: No active agent context available.";
        return false;
    }
    return true;
}

std::string report_final_result_tool::execute(agentlib::tool_context& ctx) {
    if (ctx.active_agent) {
        ctx.active_agent->set_final_result(args_.result);
    }
    return fs_utils::wrap_prompt_untrusted_data_tag("report_final_result_result", "Final result reported successfully.");
}

} // namespace tools
