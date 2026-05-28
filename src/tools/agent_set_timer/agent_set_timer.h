#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct agent_set_timer_args {
    int seconds;
};

class agent_set_timer_tool : public agentlib::llm_tool {
public:
    explicit agent_set_timer_tool(agent_set_timer_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_set_timer_args args_;
};

} // namespace tools
