#pragma once
#include <string>
#include <memory>
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

struct run_shell_command_args {
    std::string command;
    int timeout = 300;
    bool is_async = false;
};

class run_shell_command_tool : public agentlib::llm_tool {
public:
    explicit run_shell_command_tool(run_shell_command_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    run_shell_command_args args_;
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
};

class run_shell_command_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "run_shell_command"; }
    std::string get_description() const override { return "Runs an arbitrary shell command safely within the sandbox. The command will be subject to user permission approval."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"command", {
                    {"type", "string"},
                    {"description", "The exact shell command to execute."}
                }},
                {"timeout", {
                    {"type", "integer"},
                    {"description", "Optional timeout in seconds. Default is 300."}
                }},
                {"async", {
                    {"type", "boolean"},
                    {"description", "Optional. If true, runs the command in the background. Default is false."}
                }}
            }},
            {"required", nlohmann::json::array({"command"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable run_shell_command_args parsed_args_;
};

} // namespace tools
