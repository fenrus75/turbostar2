#include "list_tool_calls.h"
#include "../../agentlib/tool_registry.h"
#include <sstream>
#include <nlohmann/json.hpp>

namespace tools {

bool list_tool_calls_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string list_tool_calls_tool::execute(agentlib::tool_context& /*ctx*/) {
    auto tools_json = agentlib::tool_registry::get_instance().get_tools_json();
    
    std::ostringstream oss;
    oss << "| Tool Name | Description |\n";
    oss << "|-----------|-------------|\n";

    for (const auto& tool_node : tools_json) {
        if (tool_node.contains("function")) {
            auto func = tool_node["function"];
            std::string name = func.value("name", "unknown");
            std::string desc = func.value("description", "");
            
            // Clean up description for markdown table formatting
            for (char& c : desc) {
                if (c == '\n' || c == '\r') c = ' ';
                else if (c == '|') c = '/';
            }

            oss << "| `" << name << "` | " << desc << " |\n";
        }
    }

    return oss.str();
}

} // namespace tools