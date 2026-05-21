#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class agent_delete_todo_tool : public agentlib::llm_tool {
public:
    explicit agent_delete_todo_tool(std::string text_match);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string text_match_;
};

} // namespace tools