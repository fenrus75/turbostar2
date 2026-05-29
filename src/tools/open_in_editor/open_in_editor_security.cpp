#include "../../agentlib/tool_registry.h"
#include "open_in_editor.h"

namespace tools
{

std::unique_ptr<agentlib::llm_tool> open_in_editor_validator::create_tool_from_resolved_path(const std::string &safe_path) const
{
	return std::make_unique<open_in_editor_tool>(safe_path);
}

REGISTER_TOOL(open_in_editor_validator)

} // namespace tools
