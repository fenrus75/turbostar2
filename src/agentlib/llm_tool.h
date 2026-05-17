#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "tool_context.h"

namespace agentlib {

class llm_tool {
public:
    virtual ~llm_tool() = default;

    // Stage 2 Security check (Runtime/Contextual)
    // Returns true if the operation is permitted within the current editor context.
    virtual bool validate_runtime(const nlohmann::json& args, const tool_context& ctx, std::string& out_error) const = 0;

    // Actual execution of the tool
    virtual std::string execute(const nlohmann::json& args, tool_context& ctx) = 0;
};

} // namespace agentlib
