#pragma once
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "tool_context.h"
#include "llm_tool.h"

namespace agentlib {

class tool_validator {
public:
    virtual ~tool_validator() = default;

    // Schema definition for the LLM payload
    virtual std::string get_name() const = 0;
    virtual std::string get_description() const = 0;
    virtual nlohmann::json get_parameters_schema() const = 0;

    // Non-Virtual Interface (NVI): Enforces state and execution order.
    // Parses and validates args before the tool is allowed to be instantiated.
    bool validate_args(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) {
        is_validated_ = false;
        if (validate_args_impl(args, ctx, out_error)) {
            is_validated_ = true;
            return true;
        }
        return false;
    }

    // Instantiates the actual tool. STRICTLY FAILS if validate_args was not successful.
    std::unique_ptr<llm_tool> create_tool(const nlohmann::json& args) const {
        if (!is_validated_) {
            return nullptr;
        }
        return create_tool_impl(args);
    }

protected:
    // Derived classes MUST implement these protected methods instead of the public ones.
    virtual bool validate_args_impl(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) const = 0;
    virtual std::unique_ptr<llm_tool> create_tool_impl(const nlohmann::json& args) const = 0;

private:
    bool is_validated_ = false;
};

} // namespace agentlib
