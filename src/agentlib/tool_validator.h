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

    // Stage 1 Security check. 
    // Parses and validates args before the tool is allowed to be instantiated.
    virtual bool validate_args(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) const = 0;

    // Instantiates the actual tool. Only called if validate_args returns true.
    virtual std::unique_ptr<llm_tool> create_tool() const = 0;
};

} // namespace agentlib
