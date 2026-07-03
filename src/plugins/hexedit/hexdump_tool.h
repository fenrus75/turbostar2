#pragma once

#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"

namespace tools
{

struct hexdump_args {
	std::string requested_path;
	std::string safe_path;
	size_t start_offset{0};
	size_t size{0};
};

class hexdump_tool : public agentlib::llm_tool_action
{
      public:
	explicit hexdump_tool(hexdump_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	hexdump_args args_;
};

} // namespace tools
