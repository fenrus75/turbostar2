#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>
#include <vector>

namespace tools {

/**
 * @brief Tool to add a new task to the AI agent's internal todo list.
 */
class agent_add_todo_tool : public agentlib::llm_tool_action {
public:
    /**
     * @brief Constructs the agent_add_todo tool.
     * @param text The description of the task to add.
     */
    explicit agent_add_todo_tool(std::string text);

    /**
     * @brief Validates runtime requirements (active agent present, not read-only, non-empty text).
     */
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;

    /**
     * @brief Executes the tool to add the todo entry.
     */
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string text_;
};

} // namespace tools