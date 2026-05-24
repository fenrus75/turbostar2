#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class git_push_tool : public agentlib::llm_tool_action {
public:
    explicit git_push_tool(bool force = false);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    bool force_;
};

class git_push_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "git_push"; }
    std::string get_description() const override { return "Push the current branch to the remote repository. Note: force pushing requires explicit user approval."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"force", {
                    {"type", "boolean"},
                    {"description", "Optional: Whether to force push. Note: If set to true, the user will be prompted for explicit permission."}
                }}
            }}
        };
    }
    
    bool is_pure() const override { return false; }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;
};

} // namespace tools
