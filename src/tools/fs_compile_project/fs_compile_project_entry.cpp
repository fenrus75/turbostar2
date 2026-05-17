#include "fs_compile_project.h"
#include "../../fs_utils.h"
#include "../../config_manager.h"

namespace tools {

bool fs_compile_project_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_compile_project_tool::execute(agentlib::tool_context& /*ctx*/) {
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

    std::string output = fs_utils::execute_command_sync(cmd);
    
    // Cap output at 10,000 characters to protect context window
    if (output.length() > 10000) {
        output = output.substr(output.length() - 10000);
        output = "\n...[output truncated due to length]...\n" + output;
    }

    return "```bash\n$ " + cmd + "\n" + output + "\n```";
}

} // namespace tools
