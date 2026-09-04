#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "tool_validator.h"

namespace agentlib {

// A specialized base class for the extremely common case of a tool that
// takes exactly one string parameter. This hides all JSON parsing and schema
// generation from the derived tool.
/*

# subclasses of single_string_tool_validator

| subclass          | filename                                             |
| ----------------- | ---------------------------------------------------- | 
| fs_glob_validator | src/tools/fs_glob/fs_glob.h                          |

*/
class single_string_tool_validator : public tool_validator {
public:
    virtual ~single_string_tool_validator() = default;

    // Required overrides for the derived class
    virtual std::string get_name() const override = 0;
    virtual std::string get_description() const override = 0;
    
    // Define the single parameter (and optional aliases/defaults)
    virtual std::string get_parameter_name() const = 0;
    virtual std::string get_parameter_description() const = 0;
    virtual std::vector<std::string> get_parameter_aliases() const { return {}; }
    virtual std::optional<std::string> get_default_value() const { return std::nullopt; }

    // Stage 1 Security check, but receiving a native string instead of JSON
    // (This is declared as a pure virtual hook below)

    // --- Final implementations of the base tool_validator interface ---

    nlohmann::json get_parameters_schema() const final {
        nlohmann::json props;
        nlohmann::json param_def = {
            {"type", "string"},
            {"description", get_parameter_description()}
        };
        auto def_val = get_default_value();
        if (def_val) {
            param_def["default"] = *def_val;
        }
        props[get_parameter_name()] = param_def;

        for (const auto& alias : get_parameter_aliases()) {
            nlohmann::json alias_def = {
                {"type", "string"},
                {"description", "Alias for '" + get_parameter_name() + "': " + get_parameter_description()}
            };
            if (def_val) {
                alias_def["default"] = *def_val;
            }
            props[alias] = alias_def;
        }

        nlohmann::json schema = {
            {"type", "object"},
            {"properties", props}
        };
        if (!def_val && get_parameter_aliases().empty()) {
            schema["required"] = nlohmann::json::array({get_parameter_name()});
        }
        return schema;
    }

protected:
    bool validate_args_impl(const nlohmann::json& raw_args, const tool_context& ctx, std::string& out_error) const final {
        std::string param_name = get_parameter_name();
        auto aliases = get_parameter_aliases();
        
        std::string found_key;
        std::string val;

        if (raw_args.contains(param_name) && raw_args[param_name].is_string()) {
            found_key = param_name;
            val = raw_args[param_name].get<std::string>();
        } else {
            for (const auto& alias : aliases) {
                if (raw_args.contains(alias) && raw_args[alias].is_string()) {
                    found_key = alias;
                    val = raw_args[alias].get<std::string>();
                    break;
                }
            }
        }

        if (found_key.empty()) {
            auto def_val = get_default_value();
            if (def_val) {
                val = *def_val;
            } else {
                out_error = "Missing or invalid '" + param_name + "' string parameter.";
                return false;
            }
        }

        // Validate no unexpected other arguments are passed
        for (auto it = raw_args.begin(); it != raw_args.end(); ++it) {
            if (!found_key.empty() && it.key() != found_key) {
                out_error = "Unexpected parameter '" + it.key() + "' passed to tool.";
                return false;
            }
        }
        
        return validate_string_arg(val, ctx, out_error);
    }

    std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json& raw_args) const final {
        std::string param_name = get_parameter_name();
        if (raw_args.contains(param_name) && raw_args[param_name].is_string()) {
            return create_tool_from_string(raw_args[param_name].get<std::string>());
        }
        for (const auto& alias : get_parameter_aliases()) {
            if (raw_args.contains(alias) && raw_args[alias].is_string()) {
                return create_tool_from_string(raw_args[alias].get<std::string>());
            }
        }
        auto def_val = get_default_value();
        if (def_val) {
            return create_tool_from_string(*def_val);
        }
        return nullptr;
    }

    // New hooks for the end-user tool
    virtual bool validate_string_arg(const std::string& arg, const tool_context& ctx, std::string& out_error) const = 0;
    virtual std::unique_ptr<llm_tool> create_tool_from_string(const std::string& arg) const = 0;
};

} // namespace agentlib
