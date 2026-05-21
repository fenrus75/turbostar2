#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct wait_for_agent_args {
    int id;
};

class wait_for_agent_tool : public agentlib::llm_tool {
public:
    explicit wait_for_agent_tool(wait_for_agent_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    wait_for_agent_args args_;
};

} // namespace tools