#pragma once
#include "agentlib/llm_tool.h"
#include "agentlib/tool_context.h"
#include "agentlib/tool_validator.h"

namespace tools
{

class list_subagents_tool : public agentlib::llm_tool
{
      public:
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;
};

class list_subagents_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override { return true; }
	std::string get_name() const override { return "list_subagents"; }
	std::string get_description() const override
	{
		return "Lists all active subagents managed by the current agent. Returns a Markdown table of ID, Name, and Status.";;
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"}, {"properties", nlohmann::json::object()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;
};

} // namespace tools
