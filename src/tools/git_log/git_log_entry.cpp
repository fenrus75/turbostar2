#include "fs_utils.h"
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
	std::string cmd = std::format("git --no-pager log -n {} --oneline --no-color", args_.limit);
	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty()) {
		set_success(ctx, "No commits found");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_log_result", "Repository appears to have no commits.");
	}

	if (output.length() > 20000) {
		output = output.substr(0, 20000) + "\n...[git log truncated due to length]...";
	}

	set_success(ctx, "Log retrieved");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_log_result", "```\n" + output + "\n```");
}

} // namespace tools
