#pragma once
#include <optional>
#include <string>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

struct a2a_generate_card_with_agent_args {
	std::string requested_path;
	std::string safe_path;
	std::string output_path;
};

class a2a_generate_card_with_agent_tool : public agentlib::llm_tool_action
{
      public:
	explicit a2a_generate_card_with_agent_tool(a2a_generate_card_with_agent_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	a2a_generate_card_with_agent_args args_;
};

class a2a_generate_card_with_agent_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "a2a_generate_card_with_agent";
	}
	std::string get_description() const override
	{
		return "Spawns the a2acardgenerator subagent to convert a human-written agent .md file into a formal A2A Agent Card JSON file.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path",
		       {{"type", "string"},
			{"description",
			 "Relative path under the project workspace or VFS URI (e.g., 'src/plugins/securityagent/securityagent.md') of the agent .md definition file."}}},
		      {"output_path",
		       {{"type", "string"},
			{"description",
			 "Relative path under the project workspace or VFS URI (e.g., 'src/plugins/securityagent/securityagent.card.json'). Optional output card path (defaults to sidecar next to .md)."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

	std::string get_family() const override
	{
		return "a2a";
	}

	bool is_pure() const override
	{
		return false;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable a2a_generate_card_with_agent_args args_;
};

} // namespace tools
