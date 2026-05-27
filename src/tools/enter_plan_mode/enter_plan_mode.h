#pragma once
#include <string>
#include <memory>
#include "../../agentlib/tool_validator.h"

namespace tools {

struct enter_plan_mode_args {
    std::string reason;
};

class enter_plan_mode_tool : public agentlib::llm_tool {
public:
    explicit enter_plan_mode_tool(enter_plan_mode_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    enter_plan_mode_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class enter_plan_mode_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "enter_plan_mode"; }
    std::string get_description() const override { return "Switch to Plan Mode to safely research, design, and plan complex changes using read-only tools."; }
    bool is_pure() const override { return false; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"reason", {
                    {"type", "string"},
                    {"description", "Short reason explaining why you are entering plan mode."}
                }}
            }},
            {"required", nlohmann::json::array()}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable enter_plan_mode_args parsed_args_;
};

} // namespace tools
