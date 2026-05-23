#include "git_push.h"
#include "../../fs_utils.h"

namespace tools {

git_push_tool::git_push_tool() 
    : llm_tool_action("Pushing to remote") {}

bool git_push_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string git_push_tool::execute(agentlib::tool_context& ctx) {
    std::string cmd = "git push";
    std::string output = fs_utils::execute_command_sync(cmd);

    // git push outputs to stderr even on success, so we must check for fatal/error keywords, 
    // or specifically look for success indicators like "up-to-date" or "resolving deltas".
    if ((output.find("fatal:") == std::string::npos && output.find("error:") == std::string::npos && output.find("Everything up-to-date") != std::string::npos) || output.find("->") != std::string::npos) {
        set_success(ctx, "Git push successful");
        return "Successfully pushed to remote:\n```\n" + output + "\n```";
    }

    set_failure(ctx, "Git push failed");
    return "Failed to push to remote:\n```\n" + output + "\n```";
}

} // namespace tools
