#pragma once
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"
#include <nlohmann/json.hpp>
#include <string>

namespace tools {

class clear_all_errors_tool : public agentlib::llm_tool_action {
public:
    clear_all_errors_tool() : llm_tool_action("Clearing all errors") {}
    bool validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override { return true; }
    std::string execute(agentlib::tool_context& ctx) override;
};

class clear_all_errors_security : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "clear_all_errors"; }
    std::string get_description() const override { return "Clears all currently flagged errors and warnings from the editor UI."; }
    nlohmann::json get_parameters_schema() const override;
    bool is_pure() const override { return true; }
    
    bool validate_args_impl(
        const nlohmann::json& args, 
        const agentlib::tool_context& ctx, 
        std::string& out_error) const override;
        
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;
};

} // namespace tools
