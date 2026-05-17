#include "fs_list_dir.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

std::unique_ptr<agentlib::llm_tool> fs_list_dir_validator::create_tool_from_resolved_path(const std::string& safe_path) const {
    return std::make_unique<fs_list_dir_tool>(safe_path);
}

REGISTER_TOOL(fs_list_dir_validator)

} // namespace tools
