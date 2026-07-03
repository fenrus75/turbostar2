#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct hexwrite_args {
	std::string requested_path;
	std::string safe_path;
	size_t offset{0};
	std::string hex_data;
};

class hexwrite_tool : public agentlib::llm_tool_action
{
      public:
	explicit hexwrite_tool(hexwrite_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	hexwrite_args args_;
};

} // namespace tools
