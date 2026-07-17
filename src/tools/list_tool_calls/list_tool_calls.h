#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct list_tool_calls_args {
	std::string search;
	bool show_details = false;
};

class list_tool_calls_tool : public agentlib::llm_tool_action {
public:
    explicit list_tool_calls_tool(list_tool_calls_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    list_tool_calls_args args_;
};

} // namespace tools