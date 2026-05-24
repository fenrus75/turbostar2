#include "../../agentlib/tool_registry.h"
#include "fs_compile_file.h"

namespace tools
{

std::unique_ptr<agentlib::llm_tool> fs_compile_file_validator::create_tool_from_resolved_path(const std::string &safe_path) const
{
	return std::make_unique<fs_compile_file_tool>(safe_path);
}

REGISTER_TOOL(fs_compile_file_validator)

} // namespace tools
