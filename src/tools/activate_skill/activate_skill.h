#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/skill_manager.h"

namespace tools {

struct activate_skill_args {
    std::string name;
    agentlib::skill target_skill;
};

class activate_skill_tool : public agentlib::llm_tool {
public:
    explicit activate_skill_tool(activate_skill_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    activate_skill_args args_;
};

} // namespace tools
