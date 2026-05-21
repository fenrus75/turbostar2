#include "llm_client.h"
#include <iostream>

using json = nlohmann::json;

namespace agentlib {

llm_client::llm_client(std::shared_ptr<llm_transport> transport) : transport_(std::move(transport)) {}

llm_chat_response llm_client::send_chat(const std::vector<message>& conversation, const tool_registry* registry) {
    json payload = {
        {"model", "default-model"},
        {"messages", conversation},
        {"stream", false}
    };

    if (registry) {
        json tools_json = registry->get_tools_json();
        if (!tools_json.empty()) {
            payload["tools"] = tools_json;
            payload["tool_choice"] = "auto";
        }
    }

    std::string body = payload.dump();

    auto res = transport_->post("/v1/chat/completions", body);
    
    llm_chat_response chat_response;
    chat_response.msg.role = "assistant";

    if (res.status_code == 200) {
        try {
            json response = json::parse(res.body);
            if (response.contains("model")) {
                chat_response.model = response["model"].get<std::string>();
            }
            if (response.contains("usage")) {
                auto usage = response["usage"];
                if (usage.contains("prompt_tokens")) chat_response.usage.prompt_tokens = usage["prompt_tokens"].get<int>();
                if (usage.contains("completion_tokens")) chat_response.usage.completion_tokens = usage["completion_tokens"].get<int>();
                if (usage.contains("total_tokens")) chat_response.usage.total_tokens = usage["total_tokens"].get<int>();
            }
            if (response.contains("choices") && !response["choices"].empty()) {
                auto msg_json = response["choices"][0]["message"];
                chat_response.msg = msg_json.get<message>();
                return chat_response;
            }
        } catch (const std::exception& e) {
            chat_response.msg.content = "Error parsing response JSON: " + std::string(e.what());
            return chat_response;
        }
    }
    
    std::string target_url = transport_ ? transport_->get_base_url() : "unknown";
    chat_response.msg.content = "Error connecting to LLM server at " + target_url + ". Status: " + std::to_string(res.status_code) + "\nResponse body: " + res.body;
    return chat_response;
}

void llm_client::cancel() {
    if (transport_) {
        transport_->cancel();
    }
}

} // namespace agentlib
