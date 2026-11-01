#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct vcd_search_args {
	std::string requested_path;
	std::string safe_path;
	std::string signal;
	std::string value;
	std::string clock_signal;
	std::string edge{"posedge"}; // "posedge", "negedge", "both"
	std::string direction{"forward"}; // "forward", "backward"
	size_t limit{10};
	std::optional<size_t> start_tick;
	std::optional<size_t> end_tick;
	std::optional<uint64_t> start_time;
	std::optional<uint64_t> end_time;
};

class vcd_search_tool : public agentlib::llm_tool_action
{
      public:
	explicit vcd_search_tool(vcd_search_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	vcd_search_args args_;
};

} // namespace tools
