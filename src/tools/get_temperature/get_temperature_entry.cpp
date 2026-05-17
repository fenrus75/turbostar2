#include "get_temperature.h"

namespace tools {

std::string get_temperature_validator::get_name() const {
    return "get_temperature";
}

std::string get_temperature_validator::get_description() const {
    return "Get the current temperature in a given location";
}

nlohmann::json get_temperature_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"location", {
                {"type", "string"},
                {"description", "The location to check, e.g., San Francisco, CA or Mars"}
            }}
        }},
        {"required", nlohmann::json::array({"location"})}
    };
}

std::unique_ptr<agentlib::llm_tool> get_temperature_validator::create_tool() const {
    return std::make_unique<get_temperature_tool>();
}

std::string get_temperature_tool::execute(const nlohmann::json& args, agentlib::tool_context& /*ctx*/) {
    // In a real implementation, we might call an external API here.
    // For now, it's a dummy returning a fixed string based on the arg.
    std::string location = args.value("location", "Unknown Location");
    return "It is currently 42F in " + location + ".";
}

} // namespace tools
