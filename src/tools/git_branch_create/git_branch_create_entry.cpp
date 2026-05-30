#include "../../fs_utils.h"
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

	// git branch outputs nothing on success
	if (output.empty() || output.find("fatal:") == std::string::npos) {
		set_success(ctx, "Branch created");
		return "Successfully created branch: `" + branch_name_ + "`";
	}

	set_failure(ctx, "Failed to create branch");
	return "Failed to create branch:\n```\n" + output + "\n```";
}

} // namespace tools
