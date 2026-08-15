#include "fs_utils.h"
#include "git_restore.h"

namespace tools
{

git_restore_tool::git_restore_tool(std::string safe_path) : llm_tool_action("Restoring file to HEAD"), safe_path_(std::move(safe_path))
{
}

bool git_restore_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (safe_path_.find("://") != std::string::npos) {
		out_error = "Validation Error: git_restore cannot operate on virtual VFS paths ('" + safe_path_ + "').";
		return false;
	}
	if (safe_path_.find(".git") != std::string::npos) {
		out_error = "Security Violation: Cannot restore files inside the .git directory.";
		return false;
	}
	return true;
}

std::string git_restore_tool::execute(agentlib::tool_context &ctx)
{
	// Note: older git versions might not have 'restore', but checkout -- path works universally.
	std::string output = fs_utils::execute_command_sync("git checkout -- {}", safe_path_);

	bool has_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
			  (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

	if (!has_error) {
		set_success(ctx, "File restored");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_restore_result", "Successfully restored path: `" + safe_path_ + "`");
	}

	set_failure(ctx, "Git restore failed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_restore_result", "Failed to restore path:\n```\n" + output + "\n```");
}

} // namespace tools
