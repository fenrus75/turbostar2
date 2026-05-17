#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class fs_compile_summary_tool : public agentlib::llm_tool {
public:
    fs_compile_summary_tool() = default;

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class fs_compile_summary_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_compile_summary"; }
    std::string get_description() const override { return "Reports all files that currently have compilation errors/warnings or live LSP diagnostics."; }
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true; // No args to validate
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<fs_compile_summary_tool>();
    }
};

} // namespace tools
