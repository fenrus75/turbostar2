#include "../../fs_utils.h"
#include "git_restore.h"

namespace tools
{

git_restore_tool::git_restore_tool(std::string safe_path) : llm_tool_action("Restoring file to HEAD"), safe_path_(std::move(safe_path))
{
}

bool git_restore_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_restore_tool::execute(agentlib::tool_context &ctx)
{
	// Note: older git versions might not have 'restore', but checkout -- path works universally.
	std::string cmd = "git checkout -- '" + safe_path_ + "'";
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty() || (output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos)) {
		set_success(ctx, "File restored");
		return "Successfully restored path: `" + safe_path_ + "`";
	}

	set_failure(ctx, "Git restore failed");
	return "Failed to restore path:\n```\n" + output + "\n```";
}

} // namespace tools
