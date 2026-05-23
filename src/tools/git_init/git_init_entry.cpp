#include "git_init.h"
#include "../../fs_utils.h"
#include <filesystem>

namespace tools {

git_init_tool::git_init_tool() 
    : llm_tool_action("Initializing git repository") {}

bool git_init_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    std::filesystem::path git_dir = ctx.fs_security.get_working_directory() / ".git";
    
    // Security Check: Fail if .git directory already exists
    if (std::filesystem::exists(git_dir)) {
        out_error = "A .git directory already exists in this project. git_init is aborted.";
        return false;
    }
    return true;
}

std::string git_init_tool::execute(agentlib::tool_context& ctx) {
    std::string cmd = "git init";
    std::string output = fs_utils::execute_command_sync(cmd);

    if (output.find("Initialized empty Git repository") != std::string::npos || output.find("Reinitialized") != std::string::npos) {
        set_success(ctx, "Git repository initialized");
        return "Successfully initialized git repository:\n```\n" + output + "\n```";
    }

    set_failure(ctx, "Git init failed");
    return "Failed to initialize git repository:\n```\n" + output + "\n```";
}

} // namespace tools
