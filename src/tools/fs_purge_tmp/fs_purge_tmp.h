#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class fs_purge_tmp_tool : public agentlib::llm_tool_action {
public:
    explicit fs_purge_tmp_tool(std::string substring);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string substring_;
};

} // namespace tools
