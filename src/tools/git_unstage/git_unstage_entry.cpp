#include "../../fs_utils.h"
#include "git_unstage.h"

namespace tools
{

git_unstage_tool::git_unstage_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Unstaging files"), safe_paths_(std::move(safe_paths))
{
}

bool git_unstage_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_unstage_tool::execute(agentlib::tool_context &ctx)
{
	if (safe_paths_.empty()) {
		set_failure(ctx, "No paths provided");
		return "Failed: No paths provided to git_unstage.";
	}

	std::string cmd = "git restore --staged -- ";
	for (const auto &path : safe_paths_) {
		cmd += "'" + path + "' "; // Simple quote wrapping since paths are verified safe
	}

	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty() || (output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos)) {
		set_success(ctx, "Files unstaged");
		return "Successfully unstaged " + std::to_string(safe_paths_.size()) + " path(s).\n" +
		       (output.empty() ? "" : "```\n" + output + "\n```");
	}

	// Fallback for older git versions that don't support restore
	if (output.find("is not a git command") != std::string::npos || output.find("Unknown command") != std::string::npos) {
		cmd = "git reset HEAD -- ";
		for (const auto &path : safe_paths_) {
			cmd += "'" + path + "' ";
		}
		output = fs_utils::execute_command_sync(cmd);

		if (output.empty() || (output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos)) {
			set_success(ctx, "Files unstaged (fallback)");
			return "Successfully unstaged " + std::to_string(safe_paths_.size()) + " path(s) using fallback command.\n" +
			       (output.empty() ? "" : "```\n" + output + "\n```");
		}
	}

	set_failure(ctx, "Git unstage failed");
	return "Failed to unstage files:\n```\n" + output + "\n```";
}

} // namespace tools
