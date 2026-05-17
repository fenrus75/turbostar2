#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {

class get_temperature_tool : public agentlib::llm_tool {
public:
    explicit get_temperature_tool(std::string location);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string location_;
};

class get_temperature_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "get_temperature"; }
    std::string get_description() const override { return "Get the current temperature in a given location"; }
    std::string get_parameter_name() const override { return "location"; }
    std::string get_parameter_description() const override { return "The location to check, e.g., San Francisco, CA or Mars"; }

    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool(const std::string& arg) const override;
};

} // namespace tools
