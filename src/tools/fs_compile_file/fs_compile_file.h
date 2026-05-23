#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/single_file_tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

class fs_compile_file_tool : public agentlib::llm_tool {
public:
    explicit fs_compile_file_tool(std::string safe_path);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string safe_path_;
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
};

class fs_compile_file_validator : public agentlib::single_file_tool_validator {
public:
    std::string get_name() const override { return "fs_compile_file"; }
    std::string get_description() const override { return "Compiles a single file and returns the raw console output. Populates the workspace error list. Runs with terminal interaction."; }
    std::string get_parameter_name() const override { return "path"; }
    std::string get_parameter_description() const override { return "The path to the file to compile, relative to the project root."; }

    agentlib::access_type get_required_permission() const override { return agentlib::access_type::read; }
    
protected:
    std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const override;
};

} // namespace tools
