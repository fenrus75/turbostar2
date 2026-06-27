#include "list_subagents.h"
#include "agentlib/tool_registry.h"

namespace tools
{

bool list_subagents_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	return true;
}

std::unique_ptr<agentlib::llm_tool> list_subagents_validator::create_tool_impl(const nlohmann::json &args) const
{
	return std::make_unique<list_subagents_tool>();
}

REGISTER_TOOL(list_subagents_validator)

} // namespace tools
