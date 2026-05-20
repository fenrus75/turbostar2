#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"

namespace tools {

class list_skills_tool : public agentlib::llm_tool {
public:
    list_skills_tool() = default;

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
