#pragma once
#include "../../agentlib/tool_registry.h"

namespace tools {

struct agent_set_status_args {
    std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(agent_set_status_args, message);

class agent_set_status_tool : public agentlib::llm_tool {
public:
    explicit agent_set_status_tool(agent_set_status_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_set_status_args args_;
};

} // namespace tools
