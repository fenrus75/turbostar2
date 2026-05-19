#include "tool_registry.h"
#include <iostream>

namespace agentlib {

tool_registry& tool_registry::get_instance() {
    static tool_registry instance;
    return instance;
}

void tool_registry::register_validator(validator_factory factory) {
    // Instantiate a dummy to get its name for the registry map
    auto dummy = factory();
    if (dummy) {
        std::string name = dummy->get_name();
        validator_factories_[name] = std::move(factory);
    }
}

nlohmann::json tool_registry::get_tools_json() const {
    nlohmann::json tools_array = nlohmann::json::array();
    for (const auto& [name, factory] : validator_factories_) {
        auto validator = factory();
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
    auto it = validator_factories_.find(name);
    if (it == validator_factories_.end()) {
        return "Error: tool not found.";
    }

    // Create a transient validator instance for this execution to ensure thread-safe state!
    auto validator = it->second();

    nlohmann::json args;
    try {
        args = nlohmann::json::parse(args_json_string);
    } catch (const std::exception& e) {
        return "Error parsing tool arguments: " + std::string(e.what());
    }

    // Stage 1 Security: Pre-invocation validation
    std::string security_error;
    std::unique_ptr<agentlib::llm_tool> tool;
    
    try {
        if (!validator->validate_args(args, ctx, security_error)) {
            return "Stage 1 Security Violation: " + security_error;
        }

        // Create the tool instance (will fail if validate_args wasn't called)
        tool = validator->create_tool(args);
        if (!tool) {
            return "Error: Failed to instantiate tool. Validation state invalid.";
        }
    } catch (const std::exception& e) {
        return "Error parsing tool arguments: " + std::string(e.what());
    }

    // Stage 2 Security: Runtime/Contextual validation
    if (!tool->validate_runtime(ctx, security_error)) {
        return "Stage 2 Security Violation: " + security_error;
    }

    // Execution
    try {
        return tool->execute(ctx);
    } catch (const std::exception& e) {
        return "Execution Error: " + std::string(e.what());
    }
}

} // namespace agentlib
