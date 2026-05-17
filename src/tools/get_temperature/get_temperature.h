#pragma once
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class get_temperature_tool : public agentlib::llm_tool {
public:
    bool validate_runtime(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(const nlohmann::json& args, agentlib::tool_context& ctx) override;
};

class get_temperature_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override;
    std::string get_description() const override;
    nlohmann::json get_parameters_schema() const override;

    bool validate_args(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool() const override;
};

} // namespace tools
