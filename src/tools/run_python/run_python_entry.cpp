#include "run_python.h"
#include "../../command_runner.h"
#include "../../fs_utils.h"
#include <filesystem>
#include <fstream>
#include <random>

namespace tools {

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
    sync_command_runner runner;
    runner.apply_strict_agent_profile();
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
        // Create a temporary file in a generic /tmp location since we aren't sure about the project scratch dir yet.
        // Wait, command_runner strict profile might deny write access to /tmp directly? 
        // No, `PrivateTmp=true` means the script gets a private /tmp but the host /tmp isn't mapped inside unless we put it in the process's own /tmp.
        // If we write to host /tmp, systemd-run with PrivateTmp=true CANNOT see it.
        // So we must write it to the project directory or to a scratch space.
        
        // Let's use the project directory for now, and explicitly clean it up.
        // A better approach is to use stdin, but `uv run` expects a file to infer dependencies sometimes, though `uv run -` works for stdin.
        // Let's use stdin to avoid filesystem pollution!
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

    std::string output = runner.execute_and_get_output(full_cmd);
    
    std::string coredumps = runner.get_new_coredumps();
    if (!coredumps.empty()) {
        output += "\n\n" + coredumps;
    }

    if (output.empty()) {
        output = "Process finished successfully with no output.";
    }

    if (interaction_) {
        interaction_->set_text(output);
        if (ctx.trigger_ui_update) {
            ctx.trigger_ui_update();
        }
    }

    return output;
}

} // namespace tools