#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class git_branch_list_tool : public agentlib::llm_tool_action {
public:
    git_branch_list_tool();

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class git_branch_list_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "git_branch_list"; }
    std::string get_description() const override { return "List all git branches in the repository as a Markdown table, indicating the currently active branch."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }
    
    bool is_pure() const override { return true; }

protected:
    bool validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<git_branch_list_tool>();
    }
};

} // namespace tools
