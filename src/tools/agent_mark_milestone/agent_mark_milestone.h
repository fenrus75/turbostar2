#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct agent_mark_milestone_args {
    std::string title;
    std::string summary;
};

class agent_mark_milestone_tool : public agentlib::llm_tool {
public:
    agent_mark_milestone_tool(agent_mark_milestone_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;

private:
    agent_mark_milestone_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class agent_mark_milestone_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return false; }

    std::string get_name() const override { return "agent_mark_milestone"; }
    std::string get_description() const override {
        return "Used to signal that a major task is complete or that you are pivoting to a completely new area. This helps the system manage long-term memory and context windows efficiently by compressing old history.";
    }

    nlohmann::json get_parameters_schema() const override;

protected:
    bool validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& raw_json) const override;
    
private:
    mutable agent_mark_milestone_args args_;
};

} // namespace tools
