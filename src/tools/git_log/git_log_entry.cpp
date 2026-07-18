#include "../../fs_utils.h"
#include "git_log.h"
#include <format>

namespace tools
{

git_log_tool::git_log_tool(git_log_args args)
	: llm_tool_action("Viewing git log")
	, args_(std::move(args))
{
}

bool git_log_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_log_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = std::format("git --no-pager log -n {} --oneline --no-color", args_.count);
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty()) {
		set_success(ctx, "No commits found");
		return "Repository appears to have no commits.";
	}

	set_success(ctx, "Log retrieved");
	return "```\n" + output + "\n```";
}

} // namespace tools
