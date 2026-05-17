#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "tool_validator.h"

namespace agentlib {

// A specialized base class for the extremely common case of a tool that
// takes exactly one string parameter. This hides all JSON parsing and schema
// generation from the derived tool.
class single_string_tool_validator : public tool_validator {
public:
    virtual ~single_string_tool_validator() = default;

    // Required overrides for the derived class
    virtual std::string get_name() const override = 0;
    virtual std::string get_description() const override = 0;
    
    // Define the single parameter
    virtual std::string get_parameter_name() const = 0;
    virtual std::string get_parameter_description() const = 0;

    // Stage 1 Security check, but receiving a native string instead of JSON
    virtual bool validate_string_arg(const std::string& arg, const tool_context& ctx, std::string& out_error) const = 0;

    // Factory method, receiving a native string instead of JSON
    virtual std::unique_ptr<llm_tool> create_tool(const std::string& arg) const = 0;

    // --- Final implementations of the base tool_validator interface ---

    nlohmann::json get_parameters_schema() const final {
        return {
            {"type", "object"},
            {"properties", {
                {get_parameter_name(), {
                    {"type", "string"},
                    {"description", get_parameter_description()}
                }}
            }},
            {"required", nlohmann::json::array({get_parameter_name()})}
        };
    }

    bool validate_args(const nlohmann::json& raw_args, const tool_context& ctx, std::string& out_error) const final {
        std::string param_name = get_parameter_name();
        
        if (!raw_args.contains(param_name) || !raw_args[param_name].is_string()) {
            out_error = "Missing or invalid '" + param_name + "' string parameter.";
            return false;
        }
        
        return validate_string_arg(raw_args[param_name].get<std::string>(), ctx, out_error);
    }

    std::unique_ptr<llm_tool> create_tool(const nlohmann::json& raw_args) const final {
        return create_tool(raw_args[get_parameter_name()].get<std::string>());
    }
};

} // namespace agentlib
