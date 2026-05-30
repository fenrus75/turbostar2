#include "../../git_manager.h"
#include "../../agentlib/tool_registry.h"
#include "git_diff_from_branch.h"

namespace tools
{

bool git_diff_from_branch_validator::validate_string_arg(const std::string &arg, const agentlib::tool_context & /*ctx*/,
							 std::string &out_error) const
{
	if (!git_manager::is_valid_revision(arg)) {
		out_error = "Branch name or revision expression is invalid or contains unsafe characters.";
		return false;
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> git_diff_from_branch_validator::create_tool_from_string(const std::string &arg) const
{
	return std::make_unique<git_diff_from_branch_tool>(arg);
}

REGISTER_TOOL(git_diff_from_branch_validator)

} // namespace tools
