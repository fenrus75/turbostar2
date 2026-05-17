#include "get_temperature.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

// Register the tool with the global registry
REGISTER_TOOL(get_temperature_validator)

// Stage 1: Pre-invocation validation
bool get_temperature_validator::validate_args(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (!args.contains("location") || !args["location"].is_string()) {
        out_error = "Missing or invalid 'location' parameter.";
        return false;
    }
    
    std::string location = args["location"].get<std::string>();
    
    // Artificial security rule for demonstration: Block access to "Mars"
    if (location == "Mars" || location.find("Mars") != std::string::npos) {
        out_error = "Access to extraterrestrial locations is prohibited by policy.";
        return false;
    }

    return true;
}

// Stage 2: Runtime validation
bool get_temperature_tool::validate_runtime(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    // Nothing context-specific to restrict here for a simple weather tool.
    return true;
}

} // namespace tools
