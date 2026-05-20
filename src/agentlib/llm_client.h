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

    message send_chat(const std::vector<message>& conversation, const tool_registry* registry = nullptr);

    void cancel();

private:
    std::shared_ptr<llm_transport> transport_;
};

} // namespace agentlib
