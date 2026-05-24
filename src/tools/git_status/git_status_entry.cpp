#include <sstream>
#include "../../fs_utils.h"
#include "git_status.h"

namespace tools
{

git_status_tool::git_status_tool() : llm_tool_action("Checking git status")
{
}

bool git_status_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_status_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git --no-pager status --porcelain";
	std::string output = fs_utils::execute_command_sync(cmd);

	std::stringstream ss(output);
	std::string line;
	std::stringstream md;

	md << "## Git Status\n\n";
	md << "| Status | File |\n";
	md << "| :--- | :--- |\n";

	int count = 0;
	while (std::getline(ss, line)) {
		if (line.length() < 4)
			continue;

		std::string status_code = line.substr(0, 2);
		std::string file_path = line.substr(3);

		std::string status_desc;
		if (status_code == "??")
			status_desc = "Untracked";
		else if (status_code == " M")
			status_desc = "Modified (Unstaged)";
		else if (status_code == "M ")
			status_desc = "Modified (Staged)";
		else if (status_code == "A ")
			status_desc = "Added (Staged)";
		else if (status_code == " D")
			status_desc = "Deleted (Unstaged)";
		else if (status_code == "D ")
			status_desc = "Deleted (Staged)";
		else if (status_code == "R ")
			status_desc = "Renamed (Staged)";
		else if (status_code == "UU")
			status_desc = "Unmerged (Conflict)";
		else
			status_desc = status_code;

		md << "| " << status_desc << " | `" << file_path << "` |\n";
		count++;
	}

	if (count == 0) {
		set_success(ctx, "Working tree clean");
		return "Working tree clean. Nothing to commit.";
	}

	set_success(ctx, std::to_string(count) + " files changed");
	return md.str();
}

} // namespace tools
