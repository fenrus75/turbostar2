#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/single_file_tool_validator.h"

namespace tools {

class fs_file_size_tool : public agentlib::llm_tool_action {
public:
    explicit fs_file_size_tool(std::string safe_path);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string safe_path_;
};

class fs_file_size_validator : public agentlib::single_file_tool_validator {
public:
    std::string get_name() const override { return "fs_file_size"; }
    std::string get_description() const override { return "Get the size of a file in bytes."; }
    std::string get_parameter_name() const override { return "path"; }
    std::string get_parameter_description() const override { return "The path to the file."; }

    agentlib::access_type get_required_permission() const override { return agentlib::access_type::read; }
    bool is_pure() const override { return true; }

    std::unique_ptr<agentlib::llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const override;};

} // namespace tools
