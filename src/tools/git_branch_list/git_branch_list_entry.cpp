#include <sstream>
#include "../../fs_utils.h"
#include "git_branch_list.h"

namespace tools
{

git_branch_list_tool::git_branch_list_tool() : llm_tool_action("Listing git branches")
{
}

bool git_branch_list_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_branch_list_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git --no-pager branch --format='%(HEAD) %(refname:short)'";
	std::string output = fs_utils::execute_command_sync(cmd);

	std::stringstream ss(output);
	std::string line;
	std::stringstream md;

	md << "## Git Branches\n\n";
	md << "| Active | Branch Name |\n";
	md << "| :---: | :--- |\n";

	int count = 0;
	while (std::getline(ss, line)) {
		if (line.length() < 2)
			continue;

		std::string is_active = line.substr(0, 1) == "*" ? "✅" : "";
		std::string branch_name = line.substr(2);

		md << "| " << is_active << " | `" << branch_name << "` |\n";
		count++;
	}

	set_success(ctx, "Found " + std::to_string(count) + " branches");
	return md.str();
}

} // namespace tools
