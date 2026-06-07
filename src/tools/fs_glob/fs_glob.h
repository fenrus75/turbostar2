#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {

class fs_glob_tool : public agentlib::llm_tool_action {
public:
    explicit fs_glob_tool(std::string pattern);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string pattern_;
};

class fs_glob_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "fs_glob"; }
    std::string get_description() const override { 
        return "Returns a list of files matching a glob pattern (e.g. 'src/**/*.cpp') relative to the project root."; 
    }
    std::string get_parameter_name() const override { return "pattern"; }
    std::string get_parameter_description() const override { 
        return "The glob pattern to search for, relative to the project root (e.g. 'src/**/*.cpp' or 'docs/*.md')."; 
    }

    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override;
};

} // namespace tools
