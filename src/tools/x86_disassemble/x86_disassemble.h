#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools
{

struct x86_disassemble_args {
	std::string data;
	std::string format;
	std::string mode;
	std::string syntax;
	uint64_t address;
};

class x86_disassemble_tool : public agentlib::llm_tool_action
{
      public:
	explicit x86_disassemble_tool(x86_disassemble_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	x86_disassemble_args args_;
};

} // namespace tools
