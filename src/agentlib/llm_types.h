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
    if (j.contains("id") && !j["id"].is_null()) p.id = j["id"].get<std::string>();
    if (j.contains("type") && !j["type"].is_null()) p.type = j["type"].get<std::string>();
    if (j.contains("function") && !j["function"].is_null()) p.function = j["function"].get<function_call>();
    if (j.contains("signature") && !j["signature"].is_null()) p.signature = j["signature"].get<std::string>();
}

struct message {
    std::string role;
    std::string content;
    std::optional<std::string> reasoning_content;
    std::optional<std::string> name;
    std::optional<std::string> tool_call_id;
    std::optional<std::vector<tool_call>> tool_calls;

    // Transient episode mapping fields
    std::string episode_id;
    int episode_level{-1}; // -1 if not a paged-in episode turn
};

inline void to_json(nlohmann::json& j, const message& p) {
    j = nlohmann::json{{"role", p.role}, {"content", p.content}};
    if (p.reasoning_content) j["reasoning_content"] = *p.reasoning_content;
    if (p.name) j["name"] = *p.name;
    if (p.tool_call_id) j["tool_call_id"] = *p.tool_call_id;
    if (p.tool_calls) j["tool_calls"] = *p.tool_calls;
    if (!p.episode_id.empty()) {
        j["episode_id"] = p.episode_id;
        j["episode_level"] = p.episode_level;
    }
}

inline void from_json(const nlohmann::json& j, message& p) {
    j.at("role").get_to(p.role);
    if (j.contains("content") && !j["content"].is_null()) {
        j.at("content").get_to(p.content);
    }
    if (j.contains("reasoning_content") && !j["reasoning_content"].is_null()) {
        p.reasoning_content = j["reasoning_content"].get<std::string>();
    }
    if (j.contains("name")) p.name = j.at("name").get<std::string>();
    if (j.contains("tool_call_id")) p.tool_call_id = j.at("tool_call_id").get<std::string>();
    if (j.contains("tool_calls")) p.tool_calls = j.at("tool_calls").get<std::vector<tool_call>>();
    if (j.contains("episode_id")) {
        p.episode_id = j.at("episode_id").get<std::string>();
        p.episode_level = j.value("episode_level", -1);
    } else {
        p.episode_id = "";
        p.episode_level = -1;
    }
}

struct llm_usage {
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    int cached_tokens{0};
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
