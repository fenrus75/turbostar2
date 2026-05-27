#include "../../fs_utils.h"
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
		return "Failed: No paths provided to git add.";
	}

	std::string cmd = "git add ";
	for (const auto &path : safe_paths_) {
		cmd += "'" + path + "' "; // Simple quote wrapping since paths are verified safe
	}

	std::string output = fs_utils::execute_command_sync(cmd);

	if (output.empty() || output.find("fatal:") == std::string::npos) {
		set_success(ctx, "Files staged");
		return "Successfully staged " + std::to_string(safe_paths_.size()) + " path(s) for the next commit.\n" +
		       (output.empty() ? "" : "```\n" + output + "\n```");
	}

	set_failure(ctx, "Git add failed");
	return "Failed to stage files:\n```\n" + output + "\n```";
}

} // namespace tools
