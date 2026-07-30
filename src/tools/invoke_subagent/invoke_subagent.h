#pragma once

#include "agentlib/llm_tool.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools {

/**
 * @brief Arguments for the invoke_subagent tool.
 */
struct invoke_subagent_args {
    std::string name;
    std::string subagent_name;
    std::string profile;
    std::string task;
    std::string repository_url;
    std::string git_ref;
    bool wait{false};
    bool local_only{false};
};

/**
 * @brief Tool to spawn/invoke a sub-agent to delegate tasks to.
 */
class invoke_subagent_tool : public agentlib::llm_tool {
public:
    /**
     * @brief Constructs the invoke_subagent tool.
     */
    explicit invoke_subagent_tool(invoke_subagent_args args);

    /**
     * @brief Validates runtime requirements (active agent context available).
     */
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;

    /**
     * @brief Executes sub-agent creation and optional synchronous waiting.
     */
    std::string execute(agentlib::tool_context& ctx) override;

private:
    invoke_subagent_args args_;
};

} // namespace tools