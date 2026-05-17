#include "tool_registry.h"

namespace agentlib {

void tool_registry::add_tool(const std::string& name, 
                             const std::string& description, 
                             const nlohmann::json& parameters_schema, 
                             tool_callback callback) {
    nlohmann::json tool_schema = {
        {"type", "function"},
        {"function", {
            {"name", name},
            {"description", description},
            {"parameters", parameters_schema}
        }}
    };
    tools_[name] = {tool_schema, callback};
}

nlohmann::json tool_registry::get_tools_json() const {
    nlohmann::json tools_array = nlohmann::json::array();
    for (const auto& [name, entry] : tools_) {
        tools_array.push_back(entry.schema);
    }
    return tools_array;
}

std::string tool_registry::execute_tool(const std::string& name, const std::string& args_json_string) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        try {
            nlohmann::json args = nlohmann::json::parse(args_json_string);
            return it->second.callback(args);
        } catch (const std::exception& e) {
            return "Error parsing arguments or executing tool: " + std::string(e.what());
        }
    }
    return "Error: tool not found";
}

}
