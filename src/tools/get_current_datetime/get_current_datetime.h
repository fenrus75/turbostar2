#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"

namespace tools {

class get_current_datetime_tool : public agentlib::llm_tool {
public:
    get_current_datetime_tool() = default;

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;
};

} // namespace tools
