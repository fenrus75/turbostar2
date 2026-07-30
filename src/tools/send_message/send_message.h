#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct send_message_args {
    int id;
    std::string message;
};

class send_message_tool : public agentlib::llm_tool {
public:
    explicit send_message_tool(send_message_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    send_message_args args_;
};

} // namespace tools