#pragma once
#include <string>
#include <memory>
#include "tool_context.h"
#include "interactions/base.h"

namespace agentlib {

/*

# subclasses of llm_tool

| subclass                       | filename                                                        |
| ------------------------------ | --------------------------------------------------------------- | 
| agent_report_final_result_tool | src/tools/agent_report_final_result/agent_report_final_result.h |
| fs_replace_content_tool        | src/tools/fs_replace_content/fs_replace_content.h               |

*/

class llm_tool {
public:
    virtual ~llm_tool() = default;

    // Optional UI element for this tool's execution lifecycle.
    // If returning nullptr, the ai_agent will fall back to basic text logging.
    virtual std::shared_ptr<agent_interaction> get_interaction() const { return nullptr; }

    // Stage 2 Security check (Runtime/Contextual)
    // Returns true if the operation is permitted within the current editor context.
    virtual bool validate_runtime(const tool_context& ctx, std::string& out_error) const = 0;

    // Actual execution of the tool
    virtual std::string execute(tool_context& ctx) = 0;
};

} // namespace agentlib
