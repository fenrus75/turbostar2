#include "fs_utils.h"
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
		return fs_utils::wrap_prompt_untrusted_data_tag("git_unstage_result", "Failed: No paths provided to git_unstage.");
	}

	std::string cmd = "git restore --staged -- ";
	for (const auto &path : safe_paths_) {
		cmd += fs_utils::escape_shell_arg(path) + " ";
	}

	std::string output = fs_utils::execute_command_sync(cmd);

	bool has_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
			  (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

	if (!has_error) {
		set_success(ctx, "Files unstaged");
		std::string msg = "Successfully unstaged " + std::to_string(safe_paths_.size()) + " path(s).\n" +
				  (output.empty() ? "" : "```\n" + output + "\n```");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_unstage_result", msg);
	}

	// Fallback for older git versions that don't support restore
	if (output.find("is not a git command") != std::string::npos || output.find("Unknown command") != std::string::npos) {
		cmd = "git reset HEAD -- ";
		for (const auto &path : safe_paths_) {
			cmd += fs_utils::escape_shell_arg(path) + " ";
		}
		output = fs_utils::execute_command_sync(cmd);

		bool fallback_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
				       (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

		if (!fallback_error) {
			set_success(ctx, "Files unstaged");
			std::string msg = "Successfully unstaged " + std::to_string(safe_paths_.size()) + " path(s).\n" +
					  (output.empty() ? "" : "```\n" + output + "\n```");
			return fs_utils::wrap_prompt_untrusted_data_tag("git_unstage_result", msg);
		}
	}

	set_failure(ctx, "Git unstage failed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_unstage_result", "Failed to unstage files:\n```\n" + output + "\n```");
}

} // namespace tools
