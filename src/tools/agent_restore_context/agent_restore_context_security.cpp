#include "../../agentlib/tool_registry.h"
#include "agent_restore_context.h"

namespace tools {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_restore_context_args, episode_id, compression_level);

nlohmann::json agent_restore_context_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"episode_id", {
                {"type", "string"},
                {"description", "The ID of the episode archive to page in (e.g., 'episode_5')."}
            }},
            {"compression_level", {
                {"type", "integer"},
                {"description", "Optional. 0=Raw, 1=Think-Free (default), 2=Terminal Truncated, 3=Highly Aggressive. Adjusts how much bloat is stripped from the archive before loading it into your context."}
            }}
        }},
        {"required", nlohmann::json::array({"episode_id"})}
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