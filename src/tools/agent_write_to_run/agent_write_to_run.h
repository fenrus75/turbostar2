#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_write_to_run_args {
	int run_id{-1};
	std::string data;
};

class agent_write_to_run_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_write_to_run_tool(agent_write_to_run_args args)
	    : llm_tool_action("Writing input to application/debugger"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_write_to_run_args args_;
};

} // namespace tools
