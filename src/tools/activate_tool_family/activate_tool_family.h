#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"

namespace tools
{

struct activate_tool_family_args {
	std::string name;
};

class activate_tool_family_tool : public agentlib::llm_tool
{
      public:
	explicit activate_tool_family_tool(activate_tool_family_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	activate_tool_family_args args_;
};

} // namespace tools
