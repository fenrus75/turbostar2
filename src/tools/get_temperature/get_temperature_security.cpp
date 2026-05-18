#include "get_temperature.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

// Stage 1: Pre-invocation validation
bool get_temperature_validator::validate_string_arg(const std::string& location, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (location.empty()) {
        out_error = "Location parameter cannot be empty.";
        return false;
    }
    
    // Artificial security rule for demonstration: Block access to "Mars"
    if (location == "Mars" || location.find("Mars") != std::string::npos) {
        out_error = "Access to extraterrestrial locations is prohibited by policy.";
        return false;
    }

    return true;
}

std::unique_ptr<agentlib::llm_tool> get_temperature_validator::create_tool_from_string(const std::string& location) const {
    return std::make_unique<get_temperature_tool>(location);
}

// Register the tool with the global registry
REGISTER_TOOL(get_temperature_validator)

} // namespace tools
