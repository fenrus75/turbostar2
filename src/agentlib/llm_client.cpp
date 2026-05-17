#include "llm_client.h"
#include <httplib.h>
#include <iostream>

using json = nlohmann::json;

namespace agentlib {

llm_client::llm_client(const std::string& base_url) : base_url_(base_url) {}

message llm_client::send_chat(const std::vector<message>& conversation, const tool_registry* registry) {
    httplib::Client cli(base_url_);

    json payload = {
        {"model", "default-model"},
        {"messages", conversation},
        {"stream", false}
    };

    if (registry) {
        json tools_json = registry->get_tools_json();
        if (!tools_json.empty()) {
            payload["tools"] = tools_json;
            payload["tool_choice"] = json{
                {"type", "function"},
                {"function", {{"name", "get_temperature"}}}
            };
        }
    }

    std::string body = payload.dump();
    std::cout << "[DEBUG] Payload to LLM:\n" << payload.dump(2) << "\n";

    auto res = cli.Post("/v1/chat/completions", body, "application/json");
    
    message result_msg;
    result_msg.role = "assistant";

    if (res && res->status == 200) {
        try {
            json response = json::parse(res->body);
            std::cout << "[DEBUG] Raw response from LLM:\n" << response.dump(2) << "\n";
            if (response.contains("choices") && !response["choices"].empty()) {
                auto msg_json = response["choices"][0]["message"];
                return msg_json.get<message>();
            }
        } catch (const std::exception& e) {
            result_msg.content = "Error parsing response JSON: " + std::string(e.what());
            return result_msg;
        }
    }
    
    result_msg.content = "Error connecting to LLM server. Status: " + (res ? std::to_string(res->status) : "connection failed");
    return result_msg;
}

} // namespace agentlib
