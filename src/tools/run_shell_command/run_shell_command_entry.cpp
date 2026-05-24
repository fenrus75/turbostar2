#include "run_shell_command.h"
#include "../../fs_utils.h"
#include "../../agentlib/tool_context.h"
#include "../terminal_command_runner.h"
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <mutex>
#include <future>

namespace tools {

// In-memory session permission manager
static std::mutex g_perms_mutex;
static std::unordered_map<std::string, char> g_command_perms; // 'A' = always allow, 'D' = deny always

run_shell_command_tool::run_shell_command_tool(std::string command) : command_(std::move(command)) {
    interaction_ = std::make_shared<agentlib::interaction_terminal>("Shell Command", "Executing...");
}

std::shared_ptr<agentlib::agent_interaction> run_shell_command_tool::get_interaction() const {
    return interaction_;
}

bool run_shell_command_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string run_shell_command_tool::execute(agentlib::tool_context& ctx) {
    char rule = '?';
    {
        std::lock_guard<std::mutex> lock(g_perms_mutex);
        auto it = g_command_perms.find(command_);
        if (it != g_command_perms.end()) {
            rule = it->second;
        }
    }

    if (rule == 'D') {
        return "Error: Permission denied by user to run this command (Blacklisted).";
    }

    if (rule != 'A') {
        if (!ctx.queue) {
            return "Error: No event queue available to prompt the user for permission.";
        }

        auto promise = std::make_shared<std::promise<std::string>>();
        auto future = promise->get_future();

        editor_event ev;
        ev.type = event_type::prompt_user;
        ev.payload = "Agent wants to execute the following shell command:\n\n" + command_ + "\n\nAllow execution?";
        ev.prompt_options = {"Once", "Always", "Deny Always", "Deny"};
        ev.prompt_promise = promise;

        ctx.queue->push(ev);

        std::string response;
        try {
            response = future.get();
        } catch (const std::exception& e) {
            return std::string("Error: Failed to get user response - ") + e.what();
        }

        if (response == "Deny") {
            return "Error: Permission denied by user for this request.";
        } else if (response == "Deny Always") {
            std::lock_guard<std::mutex> lock(g_perms_mutex);
            g_command_perms[command_] = 'D';
            return "Error: Permission denied by user (Blacklisted).";
        } else if (response == "Always") {
            std::lock_guard<std::mutex> lock(g_perms_mutex);
            g_command_perms[command_] = 'A';
        } else if (response != "Once") {
            return "Error: Unknown response from user.";
        }
    }

    // Permission granted
    terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
    runner.apply_strict_agent_profile();
    runner.set_enable_crash_catcher(true);
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());

    // Execute directly: command_runner automatically escapes and wraps it via systemd-run ... -- bash -c '...'
    runner.execute(command_);

    std::string output = runner.get_final_output();

    if (output.empty()) {
        output = "Command finished successfully with no output.";
        if (interaction_) {
            interaction_->set_text(output);
            if (ctx.trigger_ui_update) {
                ctx.trigger_ui_update();
            }
        }
    }

    // Cap output at 20,000 characters to protect context window
    if (output.length() > 20000) {
        output = output.substr(output.length() - 20000);
        output = "\n...[output truncated due to length]...\n" + output;
    }

    return "```\n" + output + "\n```";
}

} // namespace tools