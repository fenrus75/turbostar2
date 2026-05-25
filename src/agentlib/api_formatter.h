#pragma once
#include <string>
#include <vector>
#include <memory>
#include "llm_types.h"
#include "tool_registry.h"
#include "ai_model.h"

namespace agentlib {

class api_formatter {
public:
    virtual ~api_formatter() = default;
    
    virtual std::string get_endpoint_path(const std::string& model_id, bool stream) const = 0;

    virtual std::string build_chat_payload(const std::string& model_id, const std::vector<message>& convo, const tool_registry* registry, bool stream) const = 0;
    
    virtual chat_delta parse_stream_chunk(const std::string& json_chunk) const = 0;
    
    virtual llm_chat_response parse_sync_response(const std::string& json_body) const = 0;

    static std::unique_ptr<api_formatter> create(api_type type);
};

class openai_formatter : public api_formatter {
public:
    std::string get_endpoint_path(const std::string& model_id, bool stream) const override;
    std::string build_chat_payload(const std::string& model_id, const std::vector<message>& convo, const tool_registry* registry, bool stream) const override;
    chat_delta parse_stream_chunk(const std::string& json_chunk) const override;
    llm_chat_response parse_sync_response(const std::string& json_body) const override;
};

class gemini_formatter : public api_formatter {
public:
    std::string get_endpoint_path(const std::string& model_id, bool stream) const override;
    std::string build_chat_payload(const std::string& model_id, const std::vector<message>& convo, const tool_registry* registry, bool stream) const override;
    chat_delta parse_stream_chunk(const std::string& json_chunk) const override;
    llm_chat_response parse_sync_response(const std::string& json_body) const override;
};

} // namespace agentlib