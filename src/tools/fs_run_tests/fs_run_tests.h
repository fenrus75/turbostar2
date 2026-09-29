#pragma once
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "agentlib/interactions/terminal.h"
#include "agentlib/llm_tool.h"
#include "agentlib/tool_validator.h"

namespace tools
{

class fs_run_tests_tool : public agentlib::llm_tool
{
      public:
	fs_run_tests_tool(std::vector<std::string> test_names = {}, int timeout = 300, std::string verbose = "auto",
			  std::string build_dir = "");

	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

	const std::string &get_verbose() const noexcept
	{
		return verbose_;
	}
	const std::string &get_build_dir() const noexcept
	{
		return build_dir_;
	}

	struct resolve_result {
		std::vector<std::string> resolved_names;
		std::vector<std::string> suggestions;
		std::vector<std::string> auto_matched_notes;
		std::vector<std::string> unresolved_queries;
		bool did_substring_expand{false};
	};
	static resolve_result resolve_test_names_detailed(std::span<const std::string> test_names,
							  const std::vector<std::string> &available);

      private:
	std::shared_ptr<agentlib::interaction_terminal> interaction_;
	std::vector<std::string> test_names_;
	int timeout_;
	std::string verbose_{"auto"};
	std::string build_dir_{""};
};

class fs_run_tests_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "fs_run_tests";
	}
	std::string get_description() const override
	{
		return "Runs the project's test suite and returns console output. Catch crashes and dumps backtraces. To discover test "
		       "names, read system://project/testlist.md (or system://project/testlist.md?search=<query>) with fs_read_lines "
		       "instead of running 'meson test --list' in a shell command.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"test_names",
			   {{"description", "Optional test name(s) to run (string or array of strings). If omitted, runs all tests. Full "
					    "test names can be discovered in system://project/testlist.md; a substring name (e.g. "
					    "\"run_shell_command\") is expanded to all matching tests."}}},
			  {"timeout", {{"type", "integer"}, {"description", "Optional timeout in seconds. Default is 300."}}},
			  {"verbose",
			   {{"description",
			     "Optional verbosity mode: 'auto' (default: compact on pass, full output on failure for single tests), 'true' "
			     "(always full output), or 'false' (always compact output). Accepts string or boolean."}}},
			  {"build_dir",
			   {{"type", "string"},
			    {"description", "Optional build directory path relative to project root (e.g. 'build' or 'build-asan'). "
					    "Defaults to the configured project build directory."}}}}}};
	}

	std::unordered_map<std::string, std::string> get_custom_parameter_aliases() const override
	{
		return {{"build_directory", "build_dir"}, {"builddir", "build_dir"}};
	}

	std::vector<agentlib::tool_example> get_examples() const override;

      protected:
	bool validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override;

      private:
	mutable std::vector<std::string> parsed_test_names_;
	mutable int parsed_timeout_{300};
	mutable std::string parsed_verbose_{"auto"};
	mutable std::string parsed_build_dir_{""};
};

} // namespace tools