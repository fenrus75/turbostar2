#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct hexinspect_args {
	std::string requested_path;
	std::string safe_path;
	size_t start_offset{0};
	size_t size{0};
};

class hexinspect_tool : public agentlib::llm_tool_action
{
      public:
	explicit hexinspect_tool(hexinspect_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	hexinspect_args args_;
};

} // namespace tools
