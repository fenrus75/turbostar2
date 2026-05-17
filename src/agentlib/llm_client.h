#pragma once
#include <string>
#include <vector>
#include "llm_types.h"
#include "tool_registry.h"

namespace agentlib {

class llm_client {
public:
    explicit llm_client(const std::string& base_url);

    message send_chat(const std::vector<message>& conversation, const tool_registry* registry = nullptr);

private:
    std::string base_url_;
};

} // namespace agentlib
