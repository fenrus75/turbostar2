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
    std::optional<std::string> signature;
};

inline void to_json(nlohmann::json& j, const tool_call& p) {
    j = nlohmann::json{{"id", p.id}, {"type", p.type}, {"function", p.function}};
    if (p.signature) j["signature"] = *p.signature;
}

inline void from_json(const nlohmann::json& j, tool_call& p) {
    j.at("id").get_to(p.id);
    j.at("type").get_to(p.type);
    j.at("function").get_to(p.function);
    if (j.contains("signature")) p.signature = j.at("signature").get<std::string>();
}

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

struct llm_usage {
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
};

struct llm_chat_response {
    message msg;
    llm_usage usage;
    std::string model;
};

struct chat_delta {
    std::string role;
    std::string content;
    std::string reasoning_content;
    std::optional<std::vector<tool_call>> tool_calls;
    bool is_final{false};
    llm_usage usage;
};

} // namespace agentlib
