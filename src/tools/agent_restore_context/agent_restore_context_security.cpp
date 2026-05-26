#include "../../agentlib/tool_registry.h"
#include "agent_restore_context.h"

namespace tools {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_restore_context_args, milestone_id, strip_reasoning);

nlohmann::json agent_restore_context_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"milestone_id", {
                {"type", "string"},
                {"description", "The exact ID of the milestone to restore (e.g., 'milestone_1716652800')."}
            }},
            {"strip_reasoning", {
                {"type", "boolean"},
                {"description", "Optional. Defaults to true. If true, strips out the agent's internal 'thinking' text from the restored history to save massive token amounts. If false, restores the raw, uncompressed archive exactly as it was."}
            }}
        }},
        {"required", nlohmann::json::array({"milestone_id"})}
    };
}

bool agent_restore_context_validator::validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    try {
        args_ = raw_json.get<agent_restore_context_args>();
        return true;
    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> agent_restore_context_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
    return std::make_unique<agent_restore_context_tool>(args_);
}

REGISTER_TOOL(agent_restore_context_validator)

} // namespace tools