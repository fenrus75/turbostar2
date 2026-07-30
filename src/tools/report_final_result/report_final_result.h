#pragma once

#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct report_final_result_args {
	std::string result;
};

class report_final_result_tool : public agentlib::llm_tool
{
      public:
	explicit report_final_result_tool(report_final_result_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	report_final_result_args args_;
};

} // namespace tools
