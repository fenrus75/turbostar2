#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class agent_list_todos_tool : public agentlib::llm_tool_action {
public:
    agent_list_todos_tool() : llm_tool_action("Listing agent todos") {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools