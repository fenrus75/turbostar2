#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct crashdump_list_args {
	int limit{20};
};

class crashdump_list_tool : public agentlib::llm_tool {
public:
    explicit crashdump_list_tool(crashdump_list_args args = {20}) : args_(args) {}

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    crashdump_list_args args_;
};

} // namespace tools