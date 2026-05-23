#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools {

class get_current_datetime_tool : public agentlib::llm_tool_action {
public:
    get_current_datetime_tool() : llm_tool_action("Getting current date and time") {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
