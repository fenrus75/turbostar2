#include "llm_client.h"
#include <iostream>

using json = nlohmann::json;

namespace agentlib {

llm_client::llm_client(std::shared_ptr<llm_transport> transport) : transport_(std::move(transport)) {}

message llm_client::send_chat(const std::vector<message>& conversation, const tool_registry* registry) {
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
    
    message result_msg;
    result_msg.role = "assistant";

    if (res.status_code == 200) {
        try {
            json response = json::parse(res.body);
            if (response.contains("choices") && !response["choices"].empty()) {
                auto msg_json = response["choices"][0]["message"];
                return msg_json.get<message>();
            }
        } catch (const std::exception& e) {
            result_msg.content = "Error parsing response JSON: " + std::string(e.what());
            return result_msg;
        }
    }
    
    result_msg.content = "Error connecting to LLM server. Status: " + std::to_string(res.status_code);
    return result_msg;
}

} // namespace agentlib
