#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

class fs_run_tests_tool : public agentlib::llm_tool {
public:
    fs_run_tests_tool();

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
};

class fs_run_tests_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_run_tests"; }
    std::string get_description() const override { return "Runs the project's test suite and returns the console output. Catch crashes and dumps backtraces. Runs with terminal interaction."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<fs_run_tests_tool>();
    }
};

} // namespace tools