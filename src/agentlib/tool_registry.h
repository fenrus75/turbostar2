#pragma once
#include <string>
#include <memory>
#include <map>
#include <nlohmann/json.hpp>
#include "tool_validator.h"
#include "tool_context.h"

namespace agentlib {

class tool_registry {
public:
    static tool_registry& get_instance();

    // Used by self-registering tools
    void register_validator(std::unique_ptr<tool_validator> validator);

    // Returns the JSON array of tools to inject into the OpenAI payload
    nlohmann::json get_tools_json() const;

    // Executes the two-stage security and execution pipeline
    std::string execute_tool(const std::string& name, const std::string& args_json_string, tool_context& ctx) const;

private:
    tool_registry() = default;
    std::map<std::string, std::unique_ptr<tool_validator>> validators_;
};

// Helper macro for static self-registration
// Usage: REGISTER_TOOL(my_validator_class)
template<typename T>
struct tool_registrar {
    tool_registrar() {
        tool_registry::get_instance().register_validator(std::make_unique<T>());
    }
};

#define REGISTER_TOOL(ValidatorClass) \
    static agentlib::tool_registrar<ValidatorClass> global_##ValidatorClass##_registrar;

} // namespace agentlib
