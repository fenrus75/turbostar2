#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_get_run_screenshot_args {
	int run_id{-1};
};

class agent_get_run_screenshot_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_get_run_screenshot_tool(agent_get_run_screenshot_args args)
	    : llm_tool_action("Capturing application/debugger output screen"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_get_run_screenshot_args args_;
};

} // namespace tools
