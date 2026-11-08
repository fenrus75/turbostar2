#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_update_timing_args {
	std::string requested_path{"."};
	std::string safe_path{"."};
	std::string requested_netlist;
	std::string safe_netlist;
	std::string target_mode{"Generic LUT4"};
};

class verilog_update_timing_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_update_timing_tool(verilog_update_timing_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_update_timing_args args_;
};

} // namespace tools
