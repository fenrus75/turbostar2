#include "run_python.h"
#include "../../command_runner.h"
#include "../../fs_utils.h"
#include "../terminal_command_runner.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <deque>
#include <sstream>

namespace tools {

class live_python_runner : public terminal_command_runner {
public:
    live_python_runner(std::shared_ptr<agentlib::interaction_terminal> interaction, 
                       std::function<void()> trigger_update)
        : terminal_command_runner(interaction, std::move(trigger_update)) {}
};

run_python_tool::run_python_tool(run_python_args args) : args_(std::move(args)) {
    std::string title = "Python Execution";
    if (args_.file_path) {
        title += " (" + *args_.file_path + ")";
    }
    interaction_ = std::make_shared<agentlib::interaction_terminal>(title, "Running...");
}

std::shared_ptr<agentlib::agent_interaction> run_python_tool::get_interaction() const {
    return interaction_;
}

bool run_python_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (args_.file_path) {
        std::string resolved_path;
        if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, out_error)) {
            return false;
        }
        if (!std::filesystem::exists(resolved_path)) {
            out_error = "File does not exist: " + resolved_path;
            return false;
        }
    }
    return true;
}

std::string run_python_tool::execute(agentlib::tool_context& ctx) {
    live_python_runner runner(interaction_, ctx.trigger_ui_update);
    runner.apply_strict_agent_profile();
    runner.set_enable_crash_catcher(true);
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());
    
    // Allow uv cache explicitly if it exists
    const char* home = std::getenv("HOME");
    if (home) {
        std::string uv_cache = std::string(home) + "/.cache/uv";
        if (std::filesystem::exists(uv_cache)) {
            runner.add_extra_rw_path(uv_cache);
        }
    }

    std::string script_path;

    if (args_.code) {
        // We will use stdin
    } else {
        std::string resolved_path;
        std::string error;
        if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, error)) {
            return "Error: " + error;
        }
        script_path = resolved_path;
    }

    std::string base_cmd;
    
    // Check if uv is available
    if (system("which uv > /dev/null 2>&1") == 0) {
        base_cmd = "PYTHONUNBUFFERED=1 uv run ";
        for (const auto& dep : args_.dependencies) {
            base_cmd += "--with '" + dep + "' ";
        }
    } else {
        base_cmd = "PYTHONUNBUFFERED=1 python3 -u ";
        if (!args_.dependencies.empty()) {
            return "Execution Error: Dependencies were requested but 'uv' is not installed on the host system.";
        }
    }

    std::string full_cmd;
    if (args_.code) {
        // Execute via stdin
        base_cmd += "- "; // Python / uv reads from stdin
        std::string escaped_code;
        for (char c : *args_.code) {
            if (c == '\'') escaped_code += "'\\''";
            else escaped_code += c;
        }
        full_cmd = "echo '" + escaped_code + "' | " + base_cmd;
    } else {
        full_cmd = base_cmd + "'" + script_path + "'";
    }

    runner.execute(full_cmd);
    std::string output = runner.get_final_output();
    
    std::string crashdumps = runner.get_new_crashdumps();
    if (!crashdumps.empty()) {
        output += "\n\n" + crashdumps;
    }

    if (output.empty()) {
        output = "Process finished successfully with no output.";
        if (interaction_) {
            interaction_->set_text(output);
            if (ctx.trigger_ui_update) {
                ctx.trigger_ui_update();
            }
        }
    }

    return output;
}

} // namespace tools