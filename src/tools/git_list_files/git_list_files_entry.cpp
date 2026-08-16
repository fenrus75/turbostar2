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
	std::string cmd = "git --no-pager ls-files -t -c -m -d -- " + fs_utils::escape_shell_arg(args_.safe_path);
	std::string output = fs_utils::execute_command_sync(cmd);

	std::stringstream ss(output);
	std::string line;

	std::unordered_map<std::string, std::string> file_status_map;
	std::vector<std::string> file_order;

	while (std::getline(ss, line)) {
		if (line.empty() || line.starts_with("Process exited with code")) {
			continue;
		}

		std::string file_path;
		std::string status_code;

		if (line.length() >= 3 && line[1] == ' ') {
			char tag = line[0];
			file_path = line.substr(2);
			switch (tag) {
			case 'C':
			case 'M':
				status_code = "MOD";
				break;
			case 'R':
				status_code = "DEL";
				break;
			case 'U':
				status_code = "UNM";
				break;
			case 'S':
				status_code = "SKIP";
				break;
			case '?':
			case 'K':
				status_code = "UNT";
				break;
			case 'H':
			default:
				status_code = "";
				break;
			}
		} else {
			file_path = line;
			status_code = "";
		}

		if (!args_.pattern.empty() && file_path.find(args_.pattern) == std::string::npos) {
			continue;
		}

		if (file_status_map.find(file_path) == file_status_map.end()) {
			file_order.push_back(file_path);
			file_status_map[file_path] = status_code;
		} else if (!status_code.empty()) {
			file_status_map[file_path] = status_code;
		}
	}

	if (file_order.empty()) {
		set_success(ctx, "No tracked files found");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_list_files_result", "No tracked files found matching criteria.");
	}

	std::stringstream md;
	md << "## Tracked Git Files (" << file_order.size() << " files)\n\n";
	md << "| File Path | Status |\n";
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
