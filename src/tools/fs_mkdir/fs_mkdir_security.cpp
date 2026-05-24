#include "../../agentlib/tool_registry.h"
#include "fs_mkdir.h"

namespace tools
{

std::unique_ptr<agentlib::llm_tool> fs_mkdir_validator::create_tool_from_resolved_path(const std::string &safe_path) const
{
	return std::make_unique<fs_mkdir_tool>(safe_path);
}

REGISTER_TOOL(fs_mkdir_validator)

} // namespace tools
