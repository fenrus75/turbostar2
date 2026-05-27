#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_string_tool_validator.h"
#include "../../fs_utils.h"

namespace tools {

class git_branch_create_tool : public agentlib::llm_tool_action {
public:
    explicit git_branch_create_tool(std::string branch_name);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string branch_name_;
};

class git_branch_create_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "git_branch_create"; }
    std::string get_description() const override { return "Create a new git branch from the current HEAD."; }
    std::string get_parameter_name() const override { return "branch_name"; }
    std::string get_parameter_description() const override { return "The name of the new branch to create."; }

    bool is_pure() const override { return false; }
    
protected:
    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        if (!fs_utils::is_shell_safe(arg, true)) {
            out_error = "Branch name contains invalid or unsafe shell characters.";
            return false;
        }
        return true;
    }
    
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override {
        return std::make_unique<git_branch_create_tool>(arg);
    }
};

} // namespace tools
