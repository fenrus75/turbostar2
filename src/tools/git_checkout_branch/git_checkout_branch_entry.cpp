#include "fs_utils.h"
#include "git_checkout_branch.h"

namespace tools
{

git_checkout_branch_tool::git_checkout_branch_tool(std::string branch_name)
    : llm_tool_action("Checking out branch " + branch_name), branch_name_(std::move(branch_name))
{
}

bool git_checkout_branch_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_checkout_branch_tool::execute(agentlib::tool_context &ctx)
{
	std::string output = fs_utils::execute_command_sync("git checkout {}", branch_name_);

	bool has_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
			  (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

	if (!has_error) {
		set_success(ctx, "Switched branch");
		std::string msg = "Successfully switched to branch: `" + branch_name_ + "`\n" + (output.empty() ? "" : "```\n" + output + "\n```");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_checkout_branch_result", msg);
	}

	set_failure(ctx, "Checkout failed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_checkout_branch_result", "Failed to checkout branch:\n```\n" + output + "\n```");
}

} // namespace tools
