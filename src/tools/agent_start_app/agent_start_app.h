#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_start_app_args {
	std::string args;
	bool debugger{false};
};

class agent_start_app_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_start_app_tool(agent_start_app_args args)
	    : llm_tool_action("Starting application"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_start_app_args args_;
};

} // namespace tools
