#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"

namespace tools
{

struct hex_inspect_range_args {
	std::string requested_path;
	int start_offset;
	int size;
	std::string safe_path;
};

class hex_inspect_range_tool : public agentlib::llm_tool_action
{
      public:
	explicit hex_inspect_range_tool(hex_inspect_range_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	hex_inspect_range_args args_;
};

} // namespace tools
