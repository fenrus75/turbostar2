#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct x86_disassemble_args {
	std::string data;
	std::string requested_path;
	std::string safe_path;
	std::string symbol;
	size_t offset{0};
	size_t size{0};
	std::string format{"auto"};
	std::string mode{"64"};
	bool mode_specified{false};
	std::string syntax{"att"};
	uint64_t address{0};
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
