#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

class sqlite_create_db_tool : public agentlib::llm_tool {
public:
    explicit sqlite_create_db_tool(std::string database);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string database_;
};

} // namespace tools