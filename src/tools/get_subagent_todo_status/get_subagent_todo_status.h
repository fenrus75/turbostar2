#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct get_subagent_todo_status_args {
    int id;
};

class get_subagent_todo_status_tool : public agentlib::llm_tool {
public:
    explicit get_subagent_todo_status_tool(get_subagent_todo_status_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    get_subagent_todo_status_args args_;
};

} // namespace tools