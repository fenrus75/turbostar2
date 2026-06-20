#pragma once
#include <string>
#include <vector>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

class security_scan_python_tool : public agentlib::llm_tool_action
{
      public:
	explicit security_scan_python_tool(std::vector<std::string> safe_paths);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	std::vector<std::string> safe_paths_;
};

class security_scan_python_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "security_scan_python";
	}

	std::string get_description() const override
	{
		return "Runs Bandit security scanner on one or more Python files and returns the result in JSON format.";
	}

	std::string get_family() const override
	{
		return ":plugin:securityagent";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"paths",
		       {{"type", "array"},
			{"items", {{"type", "string"}}},
			{"description", "List of Python file paths relative to the project root to scan (e.g. ['scripts/server.py'])."}}}}},
		    {"required", nlohmann::json::array({"paths"})}};
	}

	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable std::vector<std::string> resolved_paths_;
};

} // namespace tools
