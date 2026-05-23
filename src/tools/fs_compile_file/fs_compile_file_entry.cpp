#include "fs_compile_file.h"
#include "../../fs_utils.h"
#include "../../config_manager.h"
#include "../terminal_command_runner.h"

namespace tools {

fs_compile_file_tool::fs_compile_file_tool(std::string safe_path) : safe_path_(std::move(safe_path)) {
    interaction_ = std::make_shared<agentlib::interaction_terminal>("Compile File", "Compiling " + safe_path_ + "...");
}

std::shared_ptr<agentlib::agent_interaction> fs_compile_file_tool::get_interaction() const {
    return interaction_;
}

bool fs_compile_file_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_compile_file_tool::execute(agentlib::tool_context& ctx) {
    terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
    runner.set_enable_crash_catcher(true);
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());

    std::string build_dir = config_manager::get_instance().get_build_directory();
    std::string cmd = fs_utils::get_compile_command_for_file(safe_path_, build_dir);
    
    if (cmd.empty()) {
        return "Error: Cannot find compile command for this file in compile_commands.json.";
    }

    runner.execute(cmd);
    std::string output = runner.get_final_output();
    
    std::string crashdumps = runner.get_new_crashdumps();
    if (!crashdumps.empty()) {
        output += "\n\n" + crashdumps;
    }
    
    // Cap output at 10,000 characters to protect context window
    if (output.length() > 10000) {
        output = output.substr(output.length() - 10000);
        output = "\n...[output truncated due to length]...\n" + output;
    }

    return "```bash\n$ " + cmd + "\n" + output + "\n```";
}

} // namespace tools
