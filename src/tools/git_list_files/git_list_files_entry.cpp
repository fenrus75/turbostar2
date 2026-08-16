#include <sstream>
#include <vector>
#include "fs_utils.h"
#include "git_list_files.h"

namespace tools {

git_list_files_tool::git_list_files_tool(git_list_files_args args)
    : llm_tool_action("Listing tracked git files"), args_(std::move(args))
{
}

bool git_list_files_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_list_files_tool::execute(agentlib::tool_context &ctx)
{
	std::string cmd = "git --no-pager ls-files -- " + fs_utils::escape_shell_arg(args_.safe_path);
	std::string output = fs_utils::execute_command_sync(cmd);

	std::stringstream ss(output);
	std::string line;
	std::vector<std::string> matched_files;

	while (std::getline(ss, line)) {
		if (line.empty() || line.starts_with("Process exited with code")) {
			continue;
		}
		if (!args_.pattern.empty() && line.find(args_.pattern) == std::string::npos) {
			continue;
		}
		matched_files.push_back(line);
	}

	if (matched_files.empty()) {
		set_success(ctx, "No tracked files found");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_list_files_result", "No tracked files found matching criteria.");
	}

	std::stringstream md;
	md << "## Tracked Git Files (" << matched_files.size() << " files)\n\n";
	md << "| # | File Path |\n";
	md << "| :--- | :--- |\n";

	int count = 0;
	int omitted = 0;
	int limit = (args_.limit > 0) ? args_.limit : 500;

	for (const auto &file : matched_files) {
		if (count >= limit) {
			omitted++;
			continue;
		}
		count++;
		md << "| " << count << " | `" << file << "` |\n";
	}

	if (omitted > 0) {
		md << "\n*... [" << omitted << " additional tracked files omitted. Use path or pattern arguments to narrow search]*\n";
	}

	set_success(ctx, "Tracked git files listed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_list_files_result", md.str());
}

} // namespace tools
