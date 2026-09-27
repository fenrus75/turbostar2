#pragma once
#include <memory>
#include <string>
#include "interactions/base.h"
#include "tool_context.h"

namespace agentlib
{

/*

# subclasses of llm_tool

| subclass                       | filename                                                        |
| ------------------------------ | --------------------------------------------------------------- |
| report_final_result_tool       | src/tools/report_final_result/report_final_result.h             |
| fs_replace_content_tool        | src/tools/fs_replace_content/fs_replace_content.h               |
| git_blame_tool                | src/tools/git_blame/git_blame.h                                 |
| apply_text_filter_tool         | src/tools/apply_text_filter/apply_text_filter.h                 |
| fs_batch_read_tool            | src/tools/fs_read_lines/fs_batch_read.h                         |

*/

class llm_tool
{
      public:
	virtual ~llm_tool() = default;

	// Optional UI element for this tool's execution lifecycle.
	// If returning nullptr, the ai_agent will fall back to basic text logging.
	virtual std::shared_ptr<agent_interaction> get_interaction() const
	{
		return nullptr;
	}

	// Stage 2 Security check (Runtime/Contextual)
	// Returns true if the operation is permitted within the current editor context.
	virtual bool validate_runtime(const tool_context &ctx, std::string &out_error) const = 0;

	// Actual execution of the tool
	virtual std::string execute(tool_context &ctx) = 0;
};

} // namespace agentlib
