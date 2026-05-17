#include "tool_registry.h"
#include <iostream>

namespace agentlib {

tool_registry& tool_registry::get_instance() {
    static tool_registry instance;
    return instance;
}

void tool_registry::register_validator(std::unique_ptr<tool_validator> validator) {
    if (validator) {
        std::string name = validator->get_name();
        validators_[name] = std::move(validator);
    }
}

nlohmann::json tool_registry::get_tools_json() const {
    nlohmann::json tools_array = nlohmann::json::array();
    for (const auto& [name, validator] : validators_) {
        nlohmann::json tool_schema = {
            {"type", "function"},
            {"function", {
                {"name", validator->get_name()},
                {"description", validator->get_description()},
                {"parameters", validator->get_parameters_schema()}
            }}
        };
        tools_array.push_back(tool_schema);
    }
    return tools_array;
}

std::string tool_registry::execute_tool(const std::string& name, const std::string& args_json_string, tool_context& ctx) const {
    auto it = validators_.find(name);
    if (it == validators_.end()) {
        return "Error: tool not found.";
    }

    const auto& validator = it->second;

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(args_json_string);
    } catch (const std::exception& e) {
        return "Error parsing tool arguments: " + std::string(e.what());
    }

    // Stage 1 Security: Pre-invocation validation
    std::string security_error;
    if (!validator->validate_args(args, ctx, security_error)) {
        return "Stage 1 Security Violation: " + security_error;
    }

    // Create the tool instance
    auto tool = validator->create_tool();
    if (!tool) {
        return "Error: Failed to instantiate tool.";
    }

    // Stage 2 Security: Runtime/Contextual validation
    if (!tool->validate_runtime(args, ctx, security_error)) {
        return "Stage 2 Security Violation: " + security_error;
    }

    // Execution
    try {
        return tool->execute(args, ctx);
    } catch (const std::exception& e) {
        return "Execution Error: " + std::string(e.what());
    }
}

} // namespace agentlib
