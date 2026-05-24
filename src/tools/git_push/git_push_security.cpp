#include "git_push.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

bool git_push_validator::validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::unique_ptr<agentlib::llm_tool> git_push_validator::create_tool_impl(const nlohmann::json& args) const {
    bool force = false;
    if (args.contains("force") && args["force"].is_boolean()) {
        force = args["force"].get<bool>();
    }
    return std::make_unique<git_push_tool>(force);
}

REGISTER_TOOL(git_push_validator)

} // namespace tools
