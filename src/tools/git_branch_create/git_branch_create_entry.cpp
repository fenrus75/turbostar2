#include "fs_utils.h"
#include "git_branch_create.h"

namespace tools
{

git_branch_create_tool::git_branch_create_tool(std::string branch_name)
    : llm_tool_action("Creating git branch " + branch_name), branch_name_(std::move(branch_name))
{
}

bool git_branch_create_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_branch_create_tool::execute(agentlib::tool_context &ctx)
{
	std::string output = fs_utils::execute_command_sync("git branch {}", branch_name_);

	bool has_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
			  (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

	if (!has_error) {
		set_success(ctx, "Branch created");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_branch_create_result", "Successfully created branch: `" + branch_name_ + "`");
	}

	set_failure(ctx, "Failed to create branch");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_branch_create_result", "Failed to create branch:\n```\n" + output + "\n```");
}

} // namespace tools
