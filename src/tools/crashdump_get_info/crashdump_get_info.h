#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct crashdump_get_info_args {
    std::string crash_id;
};

class crashdump_get_info_tool : public agentlib::llm_tool {
public:
    explicit crashdump_get_info_tool(crashdump_get_info_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    crashdump_get_info_args args_;
};

} // namespace tools