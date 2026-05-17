#include "fs_file_size.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

std::unique_ptr<agentlib::llm_tool> fs_file_size_validator::create_tool_from_resolved_path(const std::string& safe_path) const {
    return std::make_unique<fs_file_size_tool>(safe_path);
}

REGISTER_TOOL(fs_file_size_validator)

} // namespace tools
