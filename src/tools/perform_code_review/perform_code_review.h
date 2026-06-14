#pragma once
#include <optional>
#include <string>
#include <vector>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools
{

struct perform_code_review_args {
	std::vector<std::string> files;
	std::string instructions;
	std::vector<std::string> todos;
	std::string result_file;
	bool async{false};
};

class perform_code_review_tool : public agentlib::llm_tool_action
{
      public:
	explicit perform_code_review_tool(perform_code_review_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	perform_code_review_args args_;
};

class perform_code_review_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "perform_code_review";
	}
	std::string get_description() const override
	{
		return "Spawns a code reviewer agent to inspect a set of files, followed by an asynchronous verification agent to confirm "
		       "findings.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"files",
		       {{"type", "array"},
			{"items", {{"type", "string"}}},
			{"description", "List of file paths relative to the project root to perform code review on."}}},
		      {"instructions",
		       {{"type", "string"}, {"description", "Optional custom review instructions or specific focus areas."}}},
		      {"todos",
		       {{"type", "array"},
			{"items", {{"type", "string"}}},
			{"description", "Optional list of todo items to assign to the code review agent."}}},
		      {"result_file",
		       {{"type", "string"},
			{"description", "Optional file path relative to project root where the final summary report will be written."}}},
		      {"async",
		       {{"type", "boolean"},
			{"description",
			 "If true, runs the code review asynchronously in the background. Defaults to false (synchronous)."}}}}},
		    {"required", nlohmann::json::array({"files"})}};
	}

	bool is_allowed_for_role(agentlib::agent_role role) const override
	{
		// Only developer and verifier roles can spawn a review subagent (prevents infinite recursion)
		return role == agentlib::agent_role::developer || role == agentlib::agent_role::verifier;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override;

      private:
	mutable perform_code_review_args args_;
};

} // namespace tools
