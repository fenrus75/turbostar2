#pragma once

#include "agentlib/llm_tool_action.h"
#include <string>

namespace tools
{

struct verilog_project_settings_args {
	std::string action{"get"}; // "get" or "set"
	std::string target;        // "ice40", "generic", "ecp5"
	std::string device;        // "hx8k"
	std::string package;       // "cb132"
	std::string top_module;    // "main"
	double freq_mhz{0.0};
	std::string pcf_file;
	std::string sv_path;
	std::string yosys_synth_args;
};

class verilog_project_settings_tool : public agentlib::llm_tool_action
{
public:
	explicit verilog_project_settings_tool(verilog_project_settings_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

private:
	verilog_project_settings_args args_;
};

} // namespace tools
