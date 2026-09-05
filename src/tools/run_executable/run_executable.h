#pragma once

#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_context.h"
#include <string>

namespace tools
{

struct run_executable_args {
	std::string binary;
	std::string args;
	bool debugger{false};
	int wait_for_time{0};
	bool collect_performance{false};
};

class run_executable_tool : public agentlib::llm_tool_action
{
      public:
	explicit run_executable_tool(run_executable_args args)
	    : llm_tool_action("Running executable"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	run_executable_args args_;
};

} // namespace tools
