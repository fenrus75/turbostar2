#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_logic_depth_args {
	std::string requested_module;
	std::string requested_file;
	std::string safe_file;
	std::string requested_netlist;
	std::string safe_netlist;
	std::string target_mode{"Generic LUT4"};
	size_t top_k{3};
	bool group_by_sink{true};
	bool collapse{true};
	int show_code{1};
};

class verilog_logic_depth_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_logic_depth_tool(verilog_logic_depth_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_logic_depth_args args_;
};

} // namespace tools
