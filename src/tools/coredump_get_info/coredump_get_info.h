#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct coredump_get_info_args {
    int pid;
};

class coredump_get_info_tool : public agentlib::llm_tool {
public:
    explicit coredump_get_info_tool(coredump_get_info_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    coredump_get_info_args args_;
};

} // namespace tools