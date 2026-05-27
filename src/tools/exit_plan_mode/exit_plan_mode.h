#pragma once
#include <string>
#include <memory>
#include "../../agentlib/tool_validator.h"

namespace tools {

struct exit_plan_mode_args {
    std::string plan_title;
    std::string plan_summary;
    bool page_out_history{false};
};

class exit_plan_mode_tool : public agentlib::llm_tool {
public:
    explicit exit_plan_mode_tool(exit_plan_mode_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    exit_plan_mode_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class exit_plan_mode_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "exit_plan_mode"; }
    std::string get_description() const override { return "Exit Plan Mode and request user approval for the finalized plan. Upon approval, modifying tools will be unlocked."; }
    bool is_pure() const override { return false; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"plan_title", {
                    {"type", "string"},
                    {"description", "A short title for the plan (1-5 words)."}
                }},
                {"plan_summary", {
                    {"type", "string"},
                    {"description", "The complete, step-by-step finalized plan to present to the user."}
                }},
                {"page_out_history", {
                    {"type", "boolean"},
                    {"description", "If true, compresses all exploratory work done since entering Plan Mode into a single milestone on disk, leaving only the plan in the active context window to save tokens. Strongly recommended."}
                }}
            }},
            {"required", nlohmann::json::array({"plan_title", "plan_summary"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable exit_plan_mode_args parsed_args_;
};

} // namespace tools
