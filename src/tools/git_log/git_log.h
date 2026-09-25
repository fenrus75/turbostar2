#pragma once
#include <string>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools
{

struct git_log_args {
	int limit = 10;
	std::string safe_path;
	std::string commit_id;
	bool show_patch{false};
	bool stat{false};
};

class git_log_tool : public agentlib::llm_tool_action
{
      public:
	explicit git_log_tool(git_log_args args);

	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	git_log_args args_;
};

class git_log_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "git_log";
	}
	std::string get_description() const override
	{
		return "View recent commit messages or inspect specific commits in the repository (git log / git show). Supports filtering "
		       "by path, viewing unified diffs (show_patch), and diffstat (stat). Use this instead of running 'git log' or 'git "
		       "show' via run_shell_command.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"commit_id",
			   {{"type", "string"},
			    {"description", "Optional specific commit hash or revision (e.g. 'HEAD', '71a56077', 'main') to inspect. If "
					    "provided, displays details for that specific commit."}}},
			  {"limit",
			   {{"type", "integer"},
			    {"description", "Optional maximum number of commits to retrieve. Defaults to 10."},
			    {"default", 10}}},
			  {"path",
			   {{"type", "string"},
			    {"description", "Optional relative path under the project workspace or VFS URI (e.g. 'src/main.cpp'). If "
					    "omitted or '.', shows commit history for the entire repository."}}},
			  {"show_patch",
			   {{"type", "boolean"},
			    {"description", "Optional. If true, include the unified diff (patch) for commits. Defaults to false."}}},
			  {"stat",
			   {{"type", "boolean"},
			    {"description", "Optional. If true, include diffstat (file changes summary). Defaults to false."}}}}}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {{"commit", "commit_id"},     {"revision", "commit_id"}, {"ref", "commit_id"},
			{"hash", "commit_id"},	     {"patch", "show_patch"},	{"diff", "show_patch"},
			{"show_diff", "show_patch"}, {"p", "show_patch"},	{"show_stat", "stat"}};
	}

	std::string get_family() const override
	{
		return "git";
	}
	bool is_pure() const override
	{
		return true;
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable git_log_args args_;
};

} // namespace tools
