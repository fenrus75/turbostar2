#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class list_tool_calls_tool : public agentlib::llm_tool_action {
public:
    list_tool_calls_tool() : llm_tool_action("Listing available tool schemas") {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools