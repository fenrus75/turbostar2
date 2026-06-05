#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools
{

struct x86_assemble_args {
	std::string instruction;
	std::string mode;
	std::string syntax;
};

class x86_assemble_tool : public agentlib::llm_tool_action
{
      public:
	explicit x86_assemble_tool(x86_assemble_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	x86_assemble_args args_;
};

} // namespace tools
