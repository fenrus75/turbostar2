#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace agentlib {

struct function_call {
    std::string name;
    std::string arguments;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(function_call, name, arguments);

struct tool_call {
    std::string id;
    std::string type;
    function_call function;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(tool_call, id, type, function);

struct message {
    std::string role;
    std::string content;
    std::optional<std::string> name;
    std::optional<std::string> tool_call_id;
    std::optional<std::vector<tool_call>> tool_calls;
};

inline void to_json(nlohmann::json& j, const message& p) {
    j = nlohmann::json{{"role", p.role}, {"content", p.content}};
    if (p.name) j["name"] = *p.name;
    if (p.tool_call_id) j["tool_call_id"] = *p.tool_call_id;
    if (p.tool_calls) j["tool_calls"] = *p.tool_calls;
}

inline void from_json(const nlohmann::json& j, message& p) {
    j.at("role").get_to(p.role);
    if (j.contains("content") && !j["content"].is_null()) {
        j.at("content").get_to(p.content);
    } else {
        p.content = "";
    }
    if (j.contains("name")) p.name = j.at("name").get<std::string>();
    if (j.contains("tool_call_id")) p.tool_call_id = j.at("tool_call_id").get<std::string>();
    if (j.contains("tool_calls")) p.tool_calls = j.at("tool_calls").get<std::vector<tool_call>>();
}

} // namespace agentlib
