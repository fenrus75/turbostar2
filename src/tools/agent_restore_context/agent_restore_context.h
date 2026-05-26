#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct agent_restore_context_args {
    std::string milestone_id;
    int compression_level{1};
};

class agent_restore_context_tool : public agentlib::llm_tool {
public:
    agent_restore_context_tool(agent_restore_context_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;

private:
    agent_restore_context_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class agent_restore_context_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return false; }

    std::string get_name() const override { return "agent_restore_context"; }
    std::string get_description() const override {
        return "Pages in a previously saved context archive (milestone). Use this if you need to resume work on an old task or look up historical context. Find the milestone_id by using the '/memory' command or reading the SYSTEM MEMORY pointers in your history.";
    }

    nlohmann::json get_parameters_schema() const override;

protected:
    bool validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& raw_json) const override;
    
private:
    mutable agent_restore_context_args args_;
};

} // namespace tools