#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>
#include <vector>
#include <optional>

namespace tools {

struct run_python_args {
    std::optional<std::string> code;
    std::optional<std::string> file_path;
    std::vector<std::string> dependencies;
};

class run_python_tool : public agentlib::llm_tool {
public:
    explicit run_python_tool(run_python_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    run_python_args args_;
};

} // namespace tools