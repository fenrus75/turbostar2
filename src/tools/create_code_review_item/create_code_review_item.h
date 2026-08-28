#pragma once
#include <optional>
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools
{

struct create_code_review_item_args {
	std::string summary;
	std::string filename;
	int line_number{0};
	std::string line_content;
	std::string severity;
	std::string description;
	std::string proposed_fix;
	std::string safe_path;
};

class create_code_review_item_tool : public agentlib::llm_tool_action
{
      public:
	explicit create_code_review_item_tool(create_code_review_item_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	create_code_review_item_args args_;
};

class create_code_review_item_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}
	std::string get_family() const override
	{
		return "base|code_review";
	}
	std::string get_name() const override
	{
		return "create_code_review_item";
	}
	std::string get_description() const override
	{
		return "Creates a new code review finding/item for a specific file and stores it in the project database.";;
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"summary", {{"type", "string"}, {"description", "A short, single-line summary of the issue."}}},
			  {"path",
			   {{"type", "string"},
			    {"description",
			     "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). The target file path containing the issue."}}},
			  {"line_number",
			   {{"type", "integer"},
			    {"description", "The 1-based line number (defaults to 0 if not pointing to a specific line)."}}},
			  {"line_content",
			   {{"type", "string"},
			    {"description", "The original line content at the time of review. If omitted and line_number > 0 is provided, "
					    "the tool will automatically load it."}}},
			  {"severity",
			   {{"type", "string"}, {"description", "The severity rating. Must be one of: nit, low, medium, high, critical."}}},
			  {"description", {{"type", "string"}, {"description", "A detailed description explaining the code issue."}}},
			  {"proposed_fix",
			   {{"type", "string"}, {"description", "A suggested code snippet or explanation on how to resolve the issue."}}}}},
			{"required", nlohmann::json::array({"summary", "path", "severity", "description"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override;

      private:
	mutable create_code_review_item_args args_;
};

} // namespace tools
