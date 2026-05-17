#include "fs_compile_info.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

std::unique_ptr<agentlib::llm_tool> fs_compile_info_validator::create_tool_from_resolved_path(const std::string& safe_path) const {
    // We pass safe_path as both the requested and safe paths since single_file_tool_validator abstracts the requested path.
    return std::make_unique<fs_compile_info_tool>(safe_path, safe_path);
}

REGISTER_TOOL(fs_compile_info_validator)

} // namespace tools
