#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/single_file_tool_validator.h"

namespace tools {

class fs_compile_info_tool : public agentlib::llm_tool {
public:
    explicit fs_compile_info_tool(std::string safe_path, std::string requested_path);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string safe_path_;
    std::string requested_path_;
    
    std::string escape_markdown(const std::string& text) const;
};

class fs_compile_info_validator : public agentlib::single_file_tool_validator {
public:
    std::string get_name() const override { return "fs_compile_info"; }
    std::string get_description() const override { return "Retrieves the compile command, last compile time, and any build/LSP diagnostics for a specific file."; }
    std::string get_parameter_name() const override { return "path"; }
    std::string get_parameter_description() const override { return "The path to the file, relative to the project root."; }

    agentlib::access_type get_required_permission() const override { return agentlib::access_type::read; }
    
protected:
    std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const override;

private:
    // requested_path_ is no longer used for validation, but we can't easily get it here since single_file_tool_validator hides the raw string.
    // For now, fs_compile_info will just use the safe_path_ in its output.
};

} // namespace tools
