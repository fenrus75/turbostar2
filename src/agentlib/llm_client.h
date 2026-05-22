#pragma once
#include <string>
#include <vector>
#include <memory>
#include "llm_types.h"
#include "tool_registry.h"
#include "llm_transport.h"

namespace agentlib {

class llm_client {
public:
    explicit llm_client(std::shared_ptr<llm_transport> transport);

    llm_chat_response send_chat(const std::vector<message>& conversation, const tool_registry* registry = nullptr);

    void send_chat_stream(const std::vector<message>& conversation, 
                          std::function<void(const chat_delta&)> callback,
                          const tool_registry* registry = nullptr);

    void cancel();

private:
    std::shared_ptr<llm_transport> transport_;
};

} // namespace agentlib
