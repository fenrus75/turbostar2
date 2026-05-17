#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/single_file_tool_validator.h"

namespace tools {

class fs_list_dir_tool : public agentlib::llm_tool {
public:
    explicit fs_list_dir_tool(std::string safe_path);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string safe_path_;

    // Helper for fast, binary-safe line counting
    std::string count_lines_if_text(const std::string& filepath) const;
};

class fs_list_dir_validator : public agentlib::single_file_tool_validator {
public:
    std::string get_name() const override { return "fs_list_dir"; }
    std::string get_description() const override { return "List the contents of a directory as a Markdown table."; }
    std::string get_parameter_name() const override { return "path"; }
    std::string get_parameter_description() const override { return "The path to the directory, relative to the project root."; }

    agentlib::access_type get_required_permission() const override { return agentlib::access_type::read; }
    
    std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const override;
};

} // namespace tools
