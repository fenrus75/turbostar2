#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {

class git_checkout_branch_tool : public agentlib::llm_tool_action {
public:
    explicit git_checkout_branch_tool(std::string branch_name);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string branch_name_;
};

class git_checkout_branch_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "git_checkout_branch"; }
    std::string get_description() const override { return "Switch to an existing git branch (git checkout <branch>)."; }
    std::string get_parameter_name() const override { return "branch_name"; }
    std::string get_parameter_description() const override { return "The name of the branch to switch to."; }

    bool is_pure() const override { return false; }
    
protected:
    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override;
};

} // namespace tools
