#include "fs_utils.h"
#include "git_add.h"

namespace tools
{

git_add_tool::git_add_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Staging files for commit"), safe_paths_(std::move(safe_paths))
{
}

bool git_add_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_add_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	if (safe_paths_.empty()) {
		set_failure(ctx, "No paths provided");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_add_result", "Failed: No paths provided to git add.");
	}

	std::string cmd = "git add ";
	for (const auto &path : safe_paths_) {
		cmd += fs_utils::escape_shell_arg(path) + " ";
	}

	std::string output = fs_utils::execute_command_sync(cmd);

	bool has_error = (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos ||
			  (output.find("Process exited with code") != std::string::npos && output.find("Process exited with code 0") == std::string::npos));

	if (!has_error) {
		set_success(ctx, "Files staged");
		std::string msg = "Successfully staged " + std::to_string(safe_paths_.size()) + " path(s) for the next commit.\n" +
				  (output.empty() ? "" : "```\n" + output + "\n```");
		return fs_utils::wrap_prompt_untrusted_data_tag("git_add_result", msg);
	}

	set_failure(ctx, "Git add failed");
	return fs_utils::wrap_prompt_untrusted_data_tag("git_add_result", "Failed to stage files:\n```\n" + output + "\n```");
}

} // namespace tools
