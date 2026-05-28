#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

class fs_list_tests_tool : public agentlib::llm_tool {
public:
    fs_list_tests_tool() = default;

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return nullptr; }
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

class fs_list_tests_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_list_tests"; }
    std::string get_description() const override { return "Returns a markdown table of all available test names in the project."; }
    bool is_pure() const override { return true; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<fs_list_tests_tool>();
    }
};

} // namespace tools
