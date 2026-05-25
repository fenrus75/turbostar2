#include "../../agentlib/tool_registry.h"
#include "agent_mark_milestone.h"

namespace tools {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_mark_milestone_args, title, summary);

nlohmann::json agent_mark_milestone_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {
                {"type", "string"},
                {"description", "A short title for the completed task or the new milestone."}
            }},
            {"summary", {
                {"type", "string"},
                {"description", "A concise summary of the work that was just completed and the goal of the new phase."}
            }}
        }},
        {"required", nlohmann::json::array({"title", "summary"})}
    };
}

bool agent_mark_milestone_validator::validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    try {
        args_ = raw_json.get<agent_mark_milestone_args>();
        return true;
    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> agent_mark_milestone_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
    return std::make_unique<agent_mark_milestone_tool>(args_);
}

REGISTER_TOOL(agent_mark_milestone_validator)

} // namespace tools