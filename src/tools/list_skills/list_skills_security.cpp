#include "list_skills.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include <nlohmann/json.hpp>

namespace tools {

class list_skills_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "list_skills"; }
    std::string get_description() const override { return "Lists all available specialized agent skills. Returns a Markdown table containing the skill name, URI, and description. Use this to discover available skills."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& /*raw_json*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*raw_json*/) const override {
        return std::make_unique<list_skills_tool>();
    }
};

REGISTER_TOOL(list_skills_validator)

} // namespace tools