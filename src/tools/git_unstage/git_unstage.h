#pragma once
#include <string>
#include <vector>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class git_unstage_tool : public agentlib::llm_tool_action {
public:
    explicit git_unstage_tool(std::vector<std::string> safe_paths);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::vector<std::string> safe_paths_;
};

class git_unstage_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "git_unstage"; }
    std::string get_description() const override { return "Unstage files that have been added to the Git index (git reset HEAD <paths>). Does not discard local file changes."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"paths", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "List of paths relative to the project root to unstage (e.g., ['src/main.cpp', 'docs/'])."}
                }}
            }},
            {"required", nlohmann::json::array({"paths"})}
        };
    }

    bool is_pure() const override { return false; }
    
protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable std::vector<std::string> resolved_paths_;
};

} // namespace tools
