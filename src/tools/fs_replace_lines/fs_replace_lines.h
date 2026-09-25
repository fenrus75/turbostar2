#pragma once
#include <string>
#include <vector>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools
{

struct edit_operation {
	int line_number;
	std::string type;	   // "add", "remove", "replace"
	std::string original_text; // Empty if not used
	std::string replace_with;  // Empty if not used
	int lines_to_remove{1};	   // Calculated from newlines in original_text
};

struct fs_replace_args {
	std::string path;
	std::string safe_path;
	std::vector<edit_operation> edits;
	std::vector<std::string> adjustment_notes;
	bool strict{false}; // If true, reject (and revert) edits that leave braces unbalanced instead of warning.
};

class fs_replace_lines_tool : public agentlib::llm_tool
{
      public:
	explicit fs_replace_lines_tool(fs_replace_args args);

	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	mutable fs_replace_args args_;
	std::shared_ptr<agentlib::agent_interaction> interaction_;

	std::string execute_disk_fallback(agentlib::tool_context &ctx);
};

class fs_replace_lines_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_replace_lines";
	}
	std::string get_description() const override
	{
		return "Surgically edit a file by providing an array of line operations (add, remove, replace). Edits MUST be sorted in "
		       "descending line_number order to prevent line-shifting offsets.";
		;
	}
	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"path",
		       {{"type", "string"},
			{"description",
			 "Path to the file to edit, relative to the project root. Alias: 'file_path', 'filepath', 'filename'."}}},
		      {"edits",
		       {{"type", "array"},
			{"description", "A list of edit operations. Operations MUST be sorted by line_number in DESCENDING order (strictly "
					"bottom to top, e.g. edit line 100, then line 50) to prevent line shifting."},
			{"items",
			 {{"type", "object"},
			  {"properties",
			   {{"line_number",
			     {{"type", "integer"}, {"description", "The 1-based line number to target. Alias: 'start_line', 'line'."}}},
			    {"end_line",
			     {{"type", "integer"}, {"description", "Optional 1-based end line number for replacing a range of lines."}}},
			    {"type",
			     {{"type", "string"},
			      {"enum", nlohmann::json::array({"add", "remove", "replace"})},
			      {"description", "The type of edit operation. Defaults to 'replace'."}}},
			    {"original_text",
			     {{"type", "string"},
			      {"description",
			       "The exact full content of the original line(s) being modified. Alias: 'old_content', 'target_content'."}}},
			    {"replace_with",
			     {{"type", "string"},
			      {"description", "The new content to insert or replace the line with. Alias: 'new_content', 'content', "
					      "'replacement'."}}}}}}}}},
		      {"start_line",
		       {{"type", "integer"},
			{"description", "Single-edit shortcut: 1-based line number to start editing at. Alias: 'line_number', 'line'."}}},
		      {"end_line",
		       {{"type", "integer"}, {"description", "Single-edit shortcut: 1-based line number to end editing at (inclusive)."}}},
		      {"new_content",
		       {{"type", "string"},
			{"description",
			 "Single-edit shortcut: the replacement content. Alias: 'replace_with', 'content', 'replacement'."}}},
		      {"original_text",
		       {{"type", "string"},
			{"description", "Single-edit shortcut: original content being replaced. Alias: 'old_content', 'target_content'."}}},
		      {"type",
		       {{"type", "string"},
			{"enum", nlohmann::json::array({"add", "remove", "replace"})},
			{"description", "Single-edit shortcut: edit type ('replace', 'add', 'remove'). Defaults to 'replace'."}}},
		      {"strict",
		       {{"type", "boolean"},
			{"description", "Optional. If true, reject (and revert) the edits when they would leave braces unbalanced, instead "
					"of applying them and only issuing a warning. Defaults to false."}}}}},
		    {"required", nlohmann::json::array({"path"})}};
	}

	std::vector<agentlib::tool_example> get_examples() const override;

	bool is_pure() const override
	{
		return false;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &raw_args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override;

      private:
	mutable fs_replace_args args_;
};

} // namespace tools
