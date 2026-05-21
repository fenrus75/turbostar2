#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct message_agent_args {
    int id;
    std::string message;
};

class message_agent_tool : public agentlib::llm_tool {
public:
    explicit message_agent_tool(message_agent_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    message_agent_args args_;
};

} // namespace tools