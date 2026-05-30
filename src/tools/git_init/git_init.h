#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/zero_argument_tool_validator.h"

namespace tools {

class git_init_tool : public agentlib::llm_tool_action {
public:
    git_init_tool();

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class git_init_validator : public agentlib::zero_argument_tool_validator {
public:
    std::string get_name() const override { return "git_init"; }
    std::string get_description() const override { return "Initialize a new Git repository in the current project root. Fails if a .git directory already exists."; }
    
    bool is_pure() const override { return false; }

protected:
    std::unique_ptr<agentlib::llm_tool> create_tool_from_zero_args() const override {
        return std::make_unique<git_init_tool>();
    }
};

} // namespace tools
