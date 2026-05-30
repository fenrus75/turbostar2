#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/zero_argument_tool_validator.h"

namespace tools {

class git_status_tool : public agentlib::llm_tool_action {
public:
    git_status_tool();

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class git_status_validator : public agentlib::zero_argument_tool_validator {
public:
    std::string get_name() const override { return "git_status"; }
    std::string get_description() const override { return "Get the git status of the project repository as a Markdown table (shows staged, unstaged, and untracked files)."; }
    
    bool is_pure() const override { return true; }

protected:
    std::unique_ptr<agentlib::llm_tool> create_tool_from_zero_args() const override {
        return std::make_unique<git_status_tool>();
    }
};

} // namespace tools
