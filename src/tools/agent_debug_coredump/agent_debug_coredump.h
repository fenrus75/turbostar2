#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_debug_coredump_args {
	std::string crash_id;
};

class agent_debug_coredump_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_debug_coredump_tool(agent_debug_coredump_args args)
	    : llm_tool_action("Debugging coredump"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_debug_coredump_args args_;
};

} // namespace tools
