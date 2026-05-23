#include "fs_compile_project.h"
#include "../../fs_utils.h"
#include "../../config_manager.h"
#include "../../crashdump_manager.h"
#include "../terminal_command_runner.h"

namespace tools {

fs_compile_project_tool::fs_compile_project_tool() {
    interaction_ = std::make_shared<agentlib::interaction_terminal>("Compilation", "Compiling project...");
}

std::shared_ptr<agentlib::agent_interaction> fs_compile_project_tool::get_interaction() const {
    return interaction_;
}

bool fs_compile_project_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_compile_project_tool::execute(agentlib::tool_context& ctx) {
    terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
    runner.set_enable_crash_catcher(true);
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());

    std::string build_system = config_manager::get_instance().get_build_system();
    std::string build_dir = config_manager::get_instance().get_build_directory();
    std::string cmd;

    if (build_system == "meson") {
        cmd = "meson compile -C " + build_dir;
    } else if (build_system == "cmake") {
        cmd = "cmake --build " + build_dir;
    } else if (build_system == "make") {
        cmd = "make -C " + build_dir;
    } else {
        cmd = build_system + " " + build_dir; // Fallback
    }

    size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
    runner.execute(cmd);
    
    std::string output = runner.get_final_output();
    runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
    size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();
    
    if (crashes_after > crashes_before) {
        output += "\n\nCRASH DETECTED: " + std::to_string(crashes_after - crashes_before) + 
                  " new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to investigate.";
    }

    // Cap output at 10,000 characters to protect context window
    if (output.length() > 10000) {
        output = output.substr(output.length() - 10000);
        output = "\n...[output truncated due to length]...\n" + output;
    }

    return "```bash\n$ " + cmd + "\n" + output + "\n```";
}

} // namespace tools
