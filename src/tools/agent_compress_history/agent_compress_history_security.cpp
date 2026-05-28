#include "../../agentlib/tool_registry.h"
#include "agent_compress_history.h"

namespace tools {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_compress_history_args, title, summary, tags, target_episode_id, include_all_prior);

nlohmann::json agent_compress_history_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {
                {"type", "string"},
                {"description", "A short title for the milestone you are archiving."}
            }},
            {"summary", {
                {"type", "string"},
                {"description", "A concise summary of the history being paged out. This summary will be injected into your context as the pointer."}
            }},
            {"tags", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "An optional list of semantic tags to label this archive."}
            }},
            {"target_episode_id", {
                {"type", "string"},
                {"description", "Optional. The exact ID of the milestone or system message (e.g., 'milestone_123') that acts as the UPPER boundary. If omitted, pages out the active/current block."}
            }},
            {"include_all_prior", {
                {"type", "boolean"},
                {"description", "Optional. If true, ignores the lower boundary and compresses everything from the target back to the system prompt."}
            }}
        }},
        {"required", nlohmann::json::array({"title", "summary"})}
    };
}

bool agent_compress_history_validator::validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    try {
        args_ = raw_json.get<agent_compress_history_args>();
        return true;
    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> agent_compress_history_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
    return std::make_unique<agent_compress_history_tool>(args_);
}

REGISTER_TOOL(agent_compress_history_validator)

} // namespace tools