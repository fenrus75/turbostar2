#include "fs_compile_file.h"
#include "../../fs_utils.h"
#include "../../config_manager.h"

namespace tools {

fs_compile_file_tool::fs_compile_file_tool(std::string safe_path) : safe_path_(std::move(safe_path)) {}

bool fs_compile_file_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_compile_file_tool::execute(agentlib::tool_context& /*ctx*/) {
    std::string build_dir = config_manager::get_instance().get_build_directory();
    std::string cmd = fs_utils::get_compile_command_for_file(safe_path_, build_dir);
    
    if (cmd.empty()) {
        return "Error: Cannot find compile command for this file in compile_commands.json.";
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
