#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

class agent_report_final_result_tool : public agentlib::llm_tool
{
      public:
	explicit agent_report_final_result_tool(std::string result);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::string result_;
};

} // namespace tools
