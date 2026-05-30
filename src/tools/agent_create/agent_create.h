#pragma once

#include "agentlib/llm_tool.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools {

/**
 * @brief Arguments for the agent_create tool.
 */
struct agent_create_args {
    std::string name;
    std::string profile;
    std::string task;
    bool wait{false};
};

/**
 * @brief Tool to spawn a sub-agent to delegate tasks to.
 */
class agent_create_tool : public agentlib::llm_tool {
public:
    /**
     * @brief Constructs the agent_create tool.
     */
    explicit agent_create_tool(agent_create_args args);

    /**
     * @brief Validates runtime requirements (active agent context available).
     */
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;

    /**
     * @brief Executes sub-agent creation and optional synchronous waiting.
     */
    std::string execute(agentlib::tool_context& ctx) override;

private:
    agent_create_args args_;
};

} // namespace tools