#pragma once

#include <optional>
#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct verilog_configure_testbench_args {
	std::string requested_testbench;
	std::string safe_testbench;
	std::vector<std::string> requested_modules;
	std::string level;
	std::optional<bool> hierarchical;
	bool remove{false};
	bool scan{false};
};

class verilog_configure_testbench_tool : public agentlib::llm_tool_action
{
      public:
	explicit verilog_configure_testbench_tool(verilog_configure_testbench_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	verilog_configure_testbench_args args_;
};

} // namespace tools
