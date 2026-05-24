#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools {

class crashdump_clear_tool : public agentlib::llm_tool_action {
public:
    crashdump_clear_tool() : llm_tool_action("Clearing all crash dumps") {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
