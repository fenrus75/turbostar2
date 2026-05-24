#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct agent_create_args {
    std::string name;
    std::string profile;
    std::string task;
    bool wait{false};
};

class agent_create_tool : public agentlib::llm_tool {
public:
    explicit agent_create_tool(agent_create_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_create_args args_;
};

} // namespace tools