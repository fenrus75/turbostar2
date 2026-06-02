#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_file_tool_validator.h"

namespace tools {

class git_diff_staged_tool : public agentlib::llm_tool_action {
public:
    explicit git_diff_staged_tool(std::string safe_path);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string safe_path_;
};

class git_diff_staged_validator : public agentlib::single_file_tool_validator {
public:
    std::string get_name() const override { return "git_diff_staged"; }
    std::string get_description() const override { return "View the staged git diff for a specific file or directory (use '.' for the entire project). Use this instead of running 'git diff --staged' or 'git diff --cached' via the shell. Returns raw patch output."; }
    std::string get_parameter_name() const override { return "path"; }
    std::string get_parameter_description() const override { return "The path to the file or directory to diff, relative to the project root (e.g., 'src/main.cpp' or '.')."; }

    agentlib::access_type get_required_permission() const override { return agentlib::access_type::read; }
    bool is_pure() const override { return true; }
    
    std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const override {
        return std::make_unique<git_diff_staged_tool>(safe_path);
    }
};

} // namespace tools
