#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools {

struct kill_subagent_args {
	int id{-1};
};

class kill_subagent_tool : public agentlib::llm_tool
{
      public:
	explicit kill_subagent_tool(kill_subagent_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	kill_subagent_args args_;
};

} // namespace tools