#include "git_commit.h"
#include "../../fs_utils.h"
#include <fstream>
#include <filesystem>
#include <system_error>

namespace tools {

git_commit_tool::git_commit_tool(std::string message) 
    : llm_tool_action("Committing staged changes"), message_(std::move(message)) {}

bool git_commit_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string git_commit_tool::execute(agentlib::tool_context& ctx) {
    // Check if there is anything to commit first
    std::string check_cmd = "git --no-pager diff --staged --quiet";
    std::string check_output = fs_utils::execute_command_sync(check_cmd);
    
    // If diff --quiet exits with 0, there are NO staged changes.
    // However, execute_command_sync doesn't return the exit code directly.
    // Instead we can use git status --porcelain.
    std::string status_cmd = "git --no-pager status --porcelain";
    std::string status_output = fs_utils::execute_command_sync(status_cmd);
    
    bool has_staged = false;
    std::stringstream ss(status_output);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.length() >= 2) {
            char staged_status = line[0];
            if (staged_status != ' ' && staged_status != '?') {
                has_staged = true;
                break;
            }
        }
    }

    if (!has_staged) {
        set_failure(ctx, "Nothing to commit");
        return "Failed: No staged changes found. Use git_add to stage files first.";
    }

    // Write commit message to a temporary file to avoid shell injection
    std::filesystem::path temp_dir = std::filesystem::path(fs_utils::get_project_tmp_dir());
    std::filesystem::path msg_file = temp_dir / ("commit_msg_" + std::to_string(std::hash<std::string>{}(message_)) + ".txt");
    
    std::ofstream out(msg_file);
    if (!out) {
        set_failure(ctx, "Internal error");
        return "Failed to create temporary commit message file.";
    }
    out << message_;
    out.close();

    std::string cmd = "git commit -F '" + msg_file.string() + "'";
    std::string output = fs_utils::execute_command_sync(cmd);

    // Clean up
    std::error_code ec;
    std::filesystem::remove(msg_file, ec);

    if (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos) {
        set_failure(ctx, "Git commit failed");
        return "Failed to commit:\n```\n" + output + "\n```";
    }

    set_success(ctx, "Commit created");
    return "Successfully created commit:\n```\n" + output + "\n```";
}

} // namespace tools
