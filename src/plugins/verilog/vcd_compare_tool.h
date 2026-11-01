#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_compare_args {
	std::string requested_path_a;
	std::string requested_path_b;
	std::string safe_path_a;
	std::string safe_path_b;
	std::vector<std::string> signals;
	std::string scope;
	std::string clock_signal;
	std::string edge{"posedge"}; // "posedge", "negedge", "both"
	std::optional<size_t> start_tick;
	std::optional<size_t> end_tick;
	std::optional<uint64_t> start_time;
	std::optional<uint64_t> end_time;
	bool strict{false};
	bool ignore_invalid{true};
};

class vcd_compare_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_compare_tool(vcd_compare_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_compare_args args_;
};

} // namespace tools
