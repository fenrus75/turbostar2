#include "../../agentlib/tool_registry.h"
#include "agent_mark_episode.h"

namespace tools {

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_mark_episode_args, title, summary, tags);

nlohmann::json agent_mark_episode_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"title", {
                {"type", "string"},
                {"description", "A short title for the completed task or the new episode."}
            }},
            {"summary", {
                {"type", "string"},
                {"description", "A concise summary of the work that was just completed and the goal of the new phase."}
            }},
            {"tags", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "An optional list of semantic tags (e.g., ['ui-refactor', 'memory-leak']) to label this episode. You can assign the same tags to multiple episodes to group them together for easy bulk-restoration later."}
            }}
        }},
        {"required", nlohmann::json::array({"title", "summary"})}
    };
}

bool agent_mark_episode_validator::validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    try {
        args_ = raw_json.get<agent_mark_episode_args>();
        if (args_.title.empty()) {
            out_error = "Title cannot be empty";
            return false;
        }
        if (args_.title.length() > 500) {
            out_error = "Title length exceeds limit of 500 characters";
            return false;
        }
        if (args_.summary.empty()) {
            out_error = "Summary cannot be empty";
            return false;
        }
        if (args_.summary.length() > 500) {
            out_error = "Summary length exceeds limit of 500 characters";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> agent_mark_episode_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
    return std::make_unique<agent_mark_episode_tool>(args_);
}

REGISTER_TOOL(agent_mark_episode_validator)

} // namespace tools