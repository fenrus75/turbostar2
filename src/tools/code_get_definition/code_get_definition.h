#pragma once
#include "../../agentlib/tool_registry.h"

namespace tools {

struct code_get_definition_args {
    std::string path;
    int line;
    int character;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(code_get_definition_args, path, line, character);

class code_get_definition_tool : public agentlib::llm_tool {
public:
    explicit code_get_definition_tool(code_get_definition_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    code_get_definition_args args_;
};

} // namespace tools
