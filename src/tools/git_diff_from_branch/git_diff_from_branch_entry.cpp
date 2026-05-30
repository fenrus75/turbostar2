#include "../../fs_utils.h"
#include "git_diff_from_branch.h"

namespace tools
{

git_diff_from_branch_tool::git_diff_from_branch_tool(std::string branch_name)
    : llm_tool_action("Viewing diff against branch " + branch_name), branch_name_(std::move(branch_name))
{
}

bool git_diff_from_branch_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_diff_from_branch_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git --no-pager diff --no-color --unified=3 " + fs_utils::escape_shell_arg(branch_name_);
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty()) {
		set_success(ctx, "No differences");
		return "Working tree is identical to branch: " + branch_name_;
	}

	if (output.length() > 30000) {
		output = output.substr(0, 30000) + "\n...[diff truncated due to length]...";
	}

	set_success(ctx, "Diff retrieved");
	return "```diff\n" + output + "\n```";
}

} // namespace tools
