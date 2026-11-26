#pragma once

#include <string>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_formal_args {
	std::string requested_module; // Optional target RTL module; if empty, runs all modules with formal/*_props.sv
	std::string mode{"all"};      // "bmc", "prove", "cover", or "all" (bmc + cover)
	int depth{20};                // BMC / cover depth in steps
	std::string solver{"auto"};   // "auto", "bitwuzla", "yices", or "z3"
	int timeout_sec{60};          // Per-module SBY timeout in seconds
};

class verilog_formal_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_formal_tool(verilog_formal_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_formal_args args_;
};

} // namespace tools
