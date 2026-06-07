#include "../../agentlib/tool_registry.h"
#include "fs_glob.h"

namespace tools {

bool fs_glob_validator::validate_string_arg(const std::string& arg, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (arg.find("..") != std::string::npos) {
        out_error = "Glob pattern cannot contain '..' directory traversal.";
        return false;
    }
    return true;
}

std::unique_ptr<agentlib::llm_tool> fs_glob_validator::create_tool_from_string(const std::string& arg) const {
    return std::make_unique<fs_glob_tool>(arg);
}

REGISTER_TOOL(fs_glob_validator)

} // namespace tools
