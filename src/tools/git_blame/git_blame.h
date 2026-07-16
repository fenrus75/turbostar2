#pragma once
#include <string>
#include <optional>
#include <vector>
#include "../../agentlib/llm_tool_action.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct git_blame_args {
	std::string requested_path;
	int start_line = 0;
	int end_line = 0;
	std::string safe_path;
};

class git_blame_tool : public agentlib::llm_tool_action {
public:
	explicit git_blame_tool(git_blame_args args);

	bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::string execute(agentlib::tool_context& ctx) override;

private:
	git_blame_args args_;
};

class git_blame_validator : public agentlib::tool_validator {
public:
	bool is_pure() const override { return true; }
	bool is_silent_by_default() const override { return false; }

	std::string get_name() const override { return "git_blame"; }
	std::string get_description() const override {
		return "View the commit-level git blame history of a file, consolidated into contiguous ranges of lines with commit summary and date. Grounding code is provided for the start line of each range to assist the agent.";
	}

    std::string get_family() const override { return "git"; }
	nlohmann::json get_parameters_schema() const override;

protected:
	bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
	mutable git_blame_args parsed_args_;
};

} // namespace tools
