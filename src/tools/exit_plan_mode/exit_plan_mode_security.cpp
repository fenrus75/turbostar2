#include "../../agentlib/tool_registry.h"
#include "exit_plan_mode.h"

namespace tools
{

bool exit_plan_mode_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const
{
    parsed_args_.plan_title = args.value("plan_title", "");
    parsed_args_.plan_summary = args.value("plan_summary", "");
    parsed_args_.page_out_history = args.value("page_out_history", false);
    
    if (parsed_args_.plan_title.empty()) {
        out_error = "plan_title cannot be empty.";
        return false;
    }
    if (parsed_args_.plan_summary.empty()) {
        out_error = "plan_summary cannot be empty.";
        return false;
    }
    
    return true;
}

std::unique_ptr<agentlib::llm_tool> exit_plan_mode_validator::create_tool_impl(const nlohmann::json& /*args*/) const
{
    return std::make_unique<exit_plan_mode_tool>(parsed_args_);
}

REGISTER_TOOL(exit_plan_mode_validator)

} // namespace tools
