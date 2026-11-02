#pragma once

#include <string>
#include <optional>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_file_info_args {
	std::string requested_path;
	std::string safe_path;
	std::string clock_signal;
	std::string scope;
	std::string filter;
	size_t max_signals{100};
	bool stats{false};
	std::optional<size_t> start_tick;
	std::optional<size_t> end_tick;
};
class vcd_file_info_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_file_info_tool(vcd_file_info_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_file_info_args args_;
};

} // namespace tools
