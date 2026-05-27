#include "../../agentlib/tool_registry.h"
#include "../../fs_utils.h"
#include "git_checkout_branch.h"

namespace tools
{

bool git_checkout_branch_validator::validate_string_arg(const std::string &arg, const agentlib::tool_context & /*ctx*/,
							std::string &out_error) const
{
	if (!fs_utils::is_shell_safe(arg, true)) {
		out_error = "Branch name contains invalid or unsafe shell characters.";
		return false;
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> git_checkout_branch_validator::create_tool_from_string(const std::string &arg) const
{
	return std::make_unique<git_checkout_branch_tool>(arg);
}

REGISTER_TOOL(git_checkout_branch_validator)

} // namespace tools
