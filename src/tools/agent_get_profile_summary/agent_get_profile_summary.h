#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"

namespace tools
{

struct agent_get_profile_summary_args {
	int limit{10};
};

class agent_get_profile_summary_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_get_profile_summary_tool(agent_get_profile_summary_args args)
	    : llm_tool_action("Retrieving performance profile summary"), args_(args)
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_get_profile_summary_args args_;
};

} // namespace tools
