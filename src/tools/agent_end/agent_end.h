#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct agent_end_args {
    int id;
};

class agent_end_tool : public agentlib::llm_tool {
public:
    explicit agent_end_tool(agent_end_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_end_args args_;
};

} // namespace tools