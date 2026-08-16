#include <sstream>
#include <unordered_map>
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
	std::string safe_path_arg = fs_utils::escape_shell_arg(args_.safe_path);

	// 1. Query tracked files baseline from git index
	std::string ls_cmd = "git --no-pager ls-files -- " + safe_path_arg;
	std::string ls_output = fs_utils::execute_command_sync(ls_cmd);

	std::stringstream ls_ss(ls_output);
	std::string line;

	std::unordered_map<std::string, std::string> file_status_map;
	std::vector<std::string> file_order;

	while (std::getline(ls_ss, line)) {
		if (line.empty() || line.starts_with("Process exited with code")) {
			continue;
		}
		if (!args_.pattern.empty() && line.find(args_.pattern) == std::string::npos) {
			continue;
		}
		if (file_status_map.find(line) == file_status_map.end()) {
			file_order.push_back(line);
			file_status_map[line] = "";
		}
	}

	// 2. Query git status --porcelain to overlay real-time staged, unstaged, rename, deletion, and addition status
	std::string status_cmd = "git --no-pager status --porcelain=v1 -- " + safe_path_arg;
	std::string status_output = fs_utils::execute_command_sync(status_cmd);

	std::stringstream status_ss(status_output);
	while (std::getline(status_ss, line)) {
		if (line.empty() || line.length() < 4 || line.starts_with("Process exited with code")) {
			continue;
		}

		char x = line[0];
		char y = line[1];

		std::string raw_path = line.substr(3);
		std::string status_code;

		if (x == 'R' || y == 'R' || raw_path.find(" -> ") != std::string::npos) {
			status_code = "REN";
		} else if (x == 'A' || y == 'A') {
			status_code = "ADD";
		} else if (x == 'D' || y == 'D') {
			status_code = "DEL";
		} else if (x == 'M' && y == 'M') {
			status_code = "MOD";
		} else if (x == 'M') {
			status_code = "STG";
		} else if (y == 'M') {
			status_code = "MOD";
		} else if (x == '?' || y == '?') {
			if (!args_.untracked) {
				continue;
			}
			status_code = "UNT";
		}

		if (!args_.pattern.empty() && raw_path.find(args_.pattern) == std::string::npos) {
			continue;
		}

		if (file_status_map.find(raw_path) == file_status_map.end()) {
			file_order.push_back(raw_path);
			file_status_map[raw_path] = status_code;
		} else {
			file_status_map[raw_path] = status_code;
		}
	}

	if (file_order.empty()) {
		set_success(ctx, "No tracked files found");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_list_files_result", "No tracked files found matching criteria.");
	}

	std::stringstream md;
	md << "## Tracked Git Files (" << file_order.size() << " files)\n\n";
	md << "| File Path | Status (blank=Tracked) |\n";
	md << "| :--- | :--- |\n";

	int count = 0;
	int omitted = 0;
	int limit = (args_.limit > 0) ? args_.limit : 500;

	for (const auto &file_path : file_order) {
		if (count >= limit) {
			omitted++;
			continue;
		}
		count++;
		md << "| `" << file_path << "` | " << file_status_map[file_path] << " |\n";
	}

	if (omitted > 0) {
		md << "\n*... [" << omitted << " additional tracked files omitted. Use path or pattern arguments to narrow search]*\n";
	}

	set_success(ctx, "Tracked git files listed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_list_files_result", md.str());
}

} // namespace tools
