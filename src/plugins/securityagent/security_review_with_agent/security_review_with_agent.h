#pragma once
#include <optional>
#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

struct security_review_with_agent_args {
	std::vector<std::string> files;
	std::string output_path;
	std::string instructions;
};

class security_review_with_agent_tool : public agentlib::llm_tool_action
{
      public:
	explicit security_review_with_agent_tool(security_review_with_agent_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	security_review_with_agent_args args_;
};

class security_review_with_agent_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "security_review_with_agent";
	}
	std::string get_description() const override
	{
		return "Spawns a dedicated security code review subagent (equipped with security scanning tools) to perform an audit of a set of files.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"files",
		       {{"type", "array"},
			{"items", {{"type", "string"}}},
			{"description", "List of file paths relative to the project root to perform security code review on."}}},
		      {"instructions",
		       {{"type", "string"}, {"description", "Optional custom instructions or specific focus areas for the security agent."}}},
		      {"output_path",
		       {{"type", "string"},
			{"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://findings.md'). Optional file path where the final markdown findings will be written."}}}}},
		    {"required", nlohmann::json::array({"files"})}};
	}

	bool is_allowed_for_agent(const agentlib::agent_properties &properties) const override
	{
		return properties.role == agentlib::agent_role::developer || properties.role == agentlib::agent_role::verifier;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable security_review_with_agent_args args_;
};

} // namespace tools
