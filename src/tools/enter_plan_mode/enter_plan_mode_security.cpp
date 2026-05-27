#include "../../agentlib/tool_registry.h"
#include "enter_plan_mode.h"

namespace tools
{

bool enter_plan_mode_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
    parsed_args_.reason = args.value("reason", "");
    return true;
}

std::unique_ptr<agentlib::llm_tool> enter_plan_mode_validator::create_tool_impl(const nlohmann::json& /*args*/) const
{
    return std::make_unique<enter_plan_mode_tool>(parsed_args_);
}

REGISTER_TOOL(enter_plan_mode_validator)

} // namespace tools
