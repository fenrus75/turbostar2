#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_signal_to_code_args {
	std::string requested_netlist;
	std::string safe_netlist;
	std::string signal;
};

class vcd_signal_to_code_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_signal_to_code_tool(vcd_signal_to_code_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_signal_to_code_args args_;
};

} // namespace tools
