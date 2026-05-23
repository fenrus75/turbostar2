#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {

class git_commit_tool : public agentlib::llm_tool_action {
public:
    explicit git_commit_tool(std::string message);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string message_;
};

class git_commit_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "git_commit"; }
    std::string get_description() const override { return "Commit the currently staged changes with the provided commit message."; }
    std::string get_parameter_name() const override { return "message"; }
    std::string get_parameter_description() const override { return "The commit message. Should follow conventional commit format if applicable."; }

    bool is_pure() const override { return false; }
    
protected:
    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        if (arg.empty()) {
            out_error = "Commit message cannot be empty.";
            return false;
        }
        return true;
    }
    
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override {
        return std::make_unique<git_commit_tool>(arg);
    }
};

} // namespace tools
