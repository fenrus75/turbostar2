#pragma once

#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_wait_for_app_args {
	int run_id{-1};
	std::string type{"ended"};
	int timeout_sec{30};
};

class agent_wait_for_app_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_wait_for_app_tool(agent_wait_for_app_args args)
	    : llm_tool_action("Waiting for application process event"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_wait_for_app_args args_;
};

} // namespace tools
