#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>
#include <vector>

namespace tools {

class agent_add_todo_tool : public agentlib::llm_tool_action {
public:
    explicit agent_add_todo_tool(std::string text);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string text_;
};

} // namespace tools