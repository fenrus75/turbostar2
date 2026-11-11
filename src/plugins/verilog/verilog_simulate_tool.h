#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_simulate_args {
	std::string requested_testbench;
	std::string safe_testbench;
	std::string requested_module;
	bool hierarchical{false};
	std::string level{"comprehensive"};
	bool level_explicit{false};
	std::string top_module;
	int timeout_sec{30};
};

class verilog_simulate_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_simulate_tool(verilog_simulate_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_simulate_args args_;
};

} // namespace tools
