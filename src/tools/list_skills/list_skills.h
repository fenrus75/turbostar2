#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools {

class list_skills_tool : public agentlib::llm_tool_action {
public:
    list_skills_tool() : llm_tool_action("Listing available skills") {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
