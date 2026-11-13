#pragma once

#include "agentlib/llm_tool_action.h"
#include <string>

namespace tools
{

struct verilog_pnr_timing_args {
	std::string requested_timing_report;
	std::string safe_timing_report;
	std::string requested_netlist;
	std::string safe_netlist;
	std::string requested_file;
	std::string safe_file;
	std::string requested_module;
	size_t top_k{3};
	int seeds{1};
	bool group_by_sink{true};
	bool reg_to_reg_only{true};
	bool collapse{true};
	bool verbose{false};
	int show_code{1};
};
class verilog_pnr_timing_tool : public agentlib::llm_tool_action
{
public:
	explicit verilog_pnr_timing_tool(verilog_pnr_timing_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

private:
	verilog_pnr_timing_args args_;
};

} // namespace tools
