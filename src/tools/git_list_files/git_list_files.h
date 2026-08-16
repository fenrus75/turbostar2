#pragma once
#include <string>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct git_list_files_args {
	std::string safe_path{"."};
	std::string pattern{""};
	int limit{500};
};

class git_list_files_tool : public agentlib::llm_tool_action {
public:
	explicit git_list_files_tool(git_list_files_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	git_list_files_args args_;
};

class git_list_files_validator : public agentlib::tool_validator {
public:
	std::string get_name() const override { return "git_list_files"; }
	std::string get_description() const override { return "List all tracked files in the Git repository index under a specified path or directory. Use this instead of running 'git ls-files' via run_shell_command."; }

	nlohmann::json get_parameters_schema() const override {
		return {
			{"type", "object"},
			{"properties", {
				{"path", {
					{"type", "string"},
					{"description", "Optional relative path or directory under project root to list tracked files for (defaults to '.')."},
					{"default", "."}
				}},
				{"pattern", {
					{"type", "string"},
					{"description", "Optional pattern or extension to filter filenames (e.g. '.cpp' or 'src/')."},
					{"default", ""}
				}},
				{"limit", {
					{"type", "integer"},
					{"description", "Optional maximum number of files to return (defaults to 500, max 5000)."},
					{"default", 500}
				}}
			}}
		};
	}

	std::string get_family() const override { return "git"; }
	bool is_pure() const override { return true; }

protected:
	bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
	mutable git_list_files_args args_;
};

} // namespace tools
