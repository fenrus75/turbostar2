#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"

namespace tools {

class agent_pop_todo_tool : public agentlib::llm_tool {
public:
    agent_pop_todo_tool() {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
