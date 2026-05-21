#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class sqlite_perform_tool : public agentlib::llm_tool {
public:
    sqlite_perform_tool(std::string database, std::string query);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string database_;
    std::string query_;
};

} // namespace tools