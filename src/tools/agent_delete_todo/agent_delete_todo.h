#pragma once

#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools {

/**
 * @brief Tool to delete a todo task matching a text pattern.
 */
class agent_delete_todo_tool : public agentlib::llm_tool_action {
public:
    /**
     * @brief Constructs the agent_delete_todo tool.
     */
    explicit agent_delete_todo_tool(std::string text_match);

    /**
     * @brief Validates runtime requirements (active agent present, not read-only, non-empty search pattern).
     */
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;

    /**
     * @brief Executes the todo item deletion.
     */
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string text_match_;
};

} // namespace tools