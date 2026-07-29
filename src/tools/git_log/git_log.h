#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct git_log_args {
    int limit = 10;
};

class git_log_tool : public agentlib::llm_tool_action {
public:
    explicit git_log_tool(git_log_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    git_log_args args_;
};

class git_log_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "git_log"; }
    std::string get_description() const override { return "View the last commit messages in the repository (git log -n <limit> --oneline)."; }

    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"limit", {
                    {"type", "integer"},
                    {"description", "Optional maximum number of commits to retrieve. Defaults to 10."},
                    {"default", 10}
                }}
            }}
        };
    }

    std::string get_family() const override { return "git"; }    
    bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
	mutable git_log_args args_;
};

} // namespace tools
