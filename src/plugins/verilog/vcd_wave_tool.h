#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_wave_args {
	std::string requested_path;
	std::string safe_path;
	std::vector<std::string> signals;
	std::string clock_signal;
	std::string edge{"posedge"}; // "posedge", "negedge", "both"
	std::optional<size_t> start_tick;
	std::optional<size_t> end_tick;
	std::optional<uint64_t> start_time;
	std::optional<uint64_t> end_time;
	size_t max_rows{50};
	bool collapse_idle{true};
};

class vcd_wave_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_wave_tool(vcd_wave_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_wave_args args_;
};

} // namespace tools
