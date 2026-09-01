#pragma once

#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_context.h"
#include <string>

namespace tools
{

struct agent_get_profile_details_args {
	std::string run_id;
	std::string file_path;
	std::string function_name;
	std::string format{"markdown"};
};

class agent_get_profile_details_tool : public agentlib::llm_tool_action
{
      public:
	explicit agent_get_profile_details_tool(agent_get_profile_details_args args)
	    : llm_tool_action("Retrieving performance profile details"), args_(std::move(args))
	{
	}

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	agent_get_profile_details_args args_;
};

} // namespace tools
