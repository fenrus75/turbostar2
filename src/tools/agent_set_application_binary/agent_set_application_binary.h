#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct agent_set_application_binary_args {
    std::string path;
};

class agent_set_application_binary_tool : public agentlib::llm_tool_action {
public:
    explicit agent_set_application_binary_tool(agent_set_application_binary_args args)
        : llm_tool_action("Setting main application binary executable"), args_(std::move(args)) {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_set_application_binary_args args_;
};

} // namespace tools
