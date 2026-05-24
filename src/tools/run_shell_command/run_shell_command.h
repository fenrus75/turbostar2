#pragma once
#include <string>
#include <memory>
#include "../../agentlib/single_string_tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

class run_shell_command_tool : public agentlib::llm_tool {
public:
    explicit run_shell_command_tool(std::string command);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string command_;
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
};

class run_shell_command_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "run_shell_command"; }
    std::string get_description() const override { return "Runs an arbitrary shell command safely within the sandbox. The command will be subject to user permission approval."; }
    std::string get_parameter_name() const override { return "command"; }
    std::string get_parameter_description() const override { return "The exact shell command to execute."; }

protected:
    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override;
};

} // namespace tools