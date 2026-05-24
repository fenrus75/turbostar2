#include "../../fs_utils.h"
#include "git_pull.h"

namespace tools
{

git_pull_tool::git_pull_tool() : llm_tool_action("Pulling from remote")
{
}

bool git_pull_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_pull_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git pull";
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos) {
		set_success(ctx, "Git pull successful");
		return "Successfully pulled from remote:\n```\n" + output + "\n```";
	}

	set_failure(ctx, "Git pull failed");
	return "Failed to pull from remote:\n```\n" + output + "\n```";
}

} // namespace tools
