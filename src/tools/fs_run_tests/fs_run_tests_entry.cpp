#include "fs_run_tests.h"
#include "../../fs_utils.h"
#include "../../config_manager.h"
#include "../terminal_command_runner.h"

namespace tools {

fs_run_tests_tool::fs_run_tests_tool() {
    interaction_ = std::make_shared<agentlib::interaction_terminal>("Test Suite", "Running tests...");
}

std::shared_ptr<agentlib::agent_interaction> fs_run_tests_tool::get_interaction() const {
    return interaction_;
}

bool fs_run_tests_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_run_tests_tool::execute(agentlib::tool_context& ctx) {
    terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
    runner.set_enable_crash_catcher(true);
    runner.set_project_dir(ctx.fs_security.get_working_directory().string());

    std::string build_system = config_manager::get_instance().get_build_system();
    std::string build_dir = config_manager::get_instance().get_build_directory();
    std::string cmd;

    if (build_system == "meson") {
        cmd = "MESON_TESTTHREADS=2 meson test -C " + build_dir;
    } else if (build_system == "cmake") {
        cmd = "ctest --test-dir " + build_dir;
    } else if (build_system == "make") {
        cmd = "make test -C " + build_dir;
    } else {
        cmd = build_system + " test " + build_dir; // Fallback
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