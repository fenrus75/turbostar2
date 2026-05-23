#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class git_log_tool : public agentlib::llm_tool_action {
public:
    git_log_tool();

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class git_log_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "git_log"; }
    std::string get_description() const override { return "View the last 10 commit messages in the repository (git log -n 10 --oneline)."; }
    
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
        return std::make_unique<git_log_tool>();
    }
};

} // namespace tools
