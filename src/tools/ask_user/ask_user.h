#pragma once
#include <string>
#include <vector>
#include "../../agentlib/llm_tool.h"

namespace tools {

struct ask_user_args {
    std::string question;
    std::vector<std::string> options;
};

class ask_user_tool : public agentlib::llm_tool {
public:
    explicit ask_user_tool(ask_user_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    ask_user_args args_;
};

} // namespace tools
