#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_compile_args {
	std::string requested_module;
	std::string safe_file;
	bool lint_only{false};
	bool synth_only{false};
	std::string requested_output_file{"tmp://synthesis.log"};
	std::string safe_output_file{"tmp://synthesis.log"};
};

class verilog_compile_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_compile_tool(verilog_compile_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_compile_args args_;
};

} // namespace tools
