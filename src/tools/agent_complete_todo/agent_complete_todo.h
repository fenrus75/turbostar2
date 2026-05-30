#pragma once

#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools {

/**
 * @brief Tool to mark a todo task as complete.
 */
class agent_complete_todo_tool : public agentlib::llm_tool_action {
public:
    /**
     * @brief Constructs the agent_complete_todo tool.
     * @param text_match The text description or unique substring of the task to mark complete.
     */
    explicit agent_complete_todo_tool(std::string text_match);

    /**
     * @brief Validates runtime requirements (active agent present, not read-only, non-empty search text).
     */
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;

    /**
     * @brief Executes the tool to mark the matching todo complete.
     */
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string text_match_;
};

} // namespace tools