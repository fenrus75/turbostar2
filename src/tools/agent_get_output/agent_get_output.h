#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct agent_get_output_args {
    int id;
};

class agent_get_output_tool : public agentlib::llm_tool {
public:
    explicit agent_get_output_tool(agent_get_output_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_get_output_args args_;
};

} // namespace tools