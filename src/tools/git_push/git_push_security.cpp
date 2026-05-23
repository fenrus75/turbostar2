#include "git_push.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

bool git_push_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (args.contains("force") && args["force"].is_boolean() && args["force"].get<bool>()) {
        out_error = "Force push is currently disabled for security reasons. Please push without force.";
        return false;
    }
    return true;
}

std::unique_ptr<agentlib::llm_tool> git_push_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
    return std::make_unique<git_push_tool>();
}

REGISTER_TOOL(git_push_validator)

} // namespace tools
