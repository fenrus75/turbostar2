#pragma once
#include <string>
#include <utility>
#include "agentlib/llm_tool_action.h"

namespace tools
{

class crashdump_clear_tool : public agentlib::llm_tool_action
{
      public:
	explicit crashdump_clear_tool(std::string crash_id = "")
	    : llm_tool_action(crash_id.empty() ? "Clearing all crash dumps" : "Clearing crash dump"), crash_id_(std::move(crash_id))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::string crash_id_;
};

} // namespace tools
