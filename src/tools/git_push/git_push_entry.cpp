#include <future>
#include "../../agentlib/tool_context.h"
#include "../../fs_utils.h"
#include "git_push.h"

namespace tools
{

git_push_tool::git_push_tool(bool force) : llm_tool_action("Pushing to remote"), force_(force)
{
}

bool git_push_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_push_tool::execute(agentlib::tool_context &ctx)
{
	if (force_) {
		if (!ctx.queue) {
			return "Error: No event queue available to prompt the user for force push permission.";
		}

		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();

		editor_event ev;
		ev.type = event_type::prompt_user;
		ev.payload = "Agent wants to execute a FORCE push (`git push --force`).\n\nThis will overwrite remote branch history.\nAre "
			     "you sure you want to allow this?";
		ev.prompt_options = {"Allow", "Deny"};
		ev.prompt_promise = promise;

		ctx.queue->push(ev);

		std::string response;
		try {
			response = future.get();
		} catch (const std::exception &e) {
			return std::string("Error: Failed to get user response - ") + e.what();
		}

		if (response != "Allow") {
			return "Error: Permission denied by user for force push.";
		}
	}

	std::string cmd = "git push";
	if (force_) {
		cmd += " --force";
	}
	std::string output = fs_utils::execute_command_sync(cmd);

	// git push outputs to stderr even on success, so we must check for fatal/error keywords,
	// or specifically look for success indicators like "up-to-date" or "resolving deltas" or "forced update".
	if ((output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos &&
	     output.find("Everything up-to-date") != std::string::npos) ||
	    output.find("->") != std::string::npos || output.find("forced update") != std::string::npos) {
		set_success(ctx, "Git push successful");
		return "Successfully pushed to remote:\n```\n" + output + "\n```";
	}

	set_failure(ctx, "Git push failed");
	return "Failed to push to remote:\n```\n" + output + "\n```";
}

} // namespace tools
