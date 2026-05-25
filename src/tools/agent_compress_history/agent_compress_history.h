#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct agent_compress_history_args {
    std::string title;
    std::string summary;
    std::vector<std::string> tags;
    std::string target_milestone_id;
    bool include_all_prior{false};
};

class agent_compress_history_tool : public agentlib::llm_tool {
public:
    agent_compress_history_tool(agent_compress_history_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;

private:
    agent_compress_history_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

class agent_compress_history_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return false; }

    std::string get_name() const override { return "agent_compress_history"; }
    std::string get_description() const override {
        return "Proactively pages out all conversational history prior to this tool call into a saved milestone archive. This frees up your context window. A highly dense pointer message will replace the old history, allowing you to restore it later if needed.";
    }

    nlohmann::json get_parameters_schema() const override;

protected:
    bool validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& raw_json) const override;
    
private:
    mutable agent_compress_history_args args_;
};

} // namespace tools