#pragma once
#include <string>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace agentlib {

class tool_registry {
public:
    using tool_callback = std::function<std::string(const nlohmann::json& args)>;

    void add_tool(const std::string& name, 
                  const std::string& description, 
                  const nlohmann::json& parameters_schema, 
                  tool_callback callback);

    nlohmann::json get_tools_json() const;

    std::string execute_tool(const std::string& name, const std::string& args_json_string) const;

private:
    struct tool_entry {
        nlohmann::json schema;
        tool_callback callback;
    };
    std::map<std::string, tool_entry> tools_;
};

}
