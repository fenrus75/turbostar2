#include "../../fs_utils.h"
#include "git_diff_unstaged.h"

namespace tools
{

git_diff_unstaged_tool::git_diff_unstaged_tool(std::string safe_path)
    : llm_tool_action("Viewing unstaged git diff"), safe_path_(std::move(safe_path))
{
}

bool git_diff_unstaged_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_diff_unstaged_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git --no-pager diff --no-color --unified=3 -- " + safe_path_;
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty()) {
		set_success(ctx, "No unstaged changes");
		return "No unstaged changes found for: " + safe_path_;
	}

	// Truncate if insanely large, preserving the end which is usually most relevant, or beginning
	if (output.length() > 30000) {
		output = output.substr(0, 30000) + "\n...[diff truncated due to length]...";
	}

	set_success(ctx, "Diff retrieved");
	return "```diff\n" + output + "\n```";
}

} // namespace tools
