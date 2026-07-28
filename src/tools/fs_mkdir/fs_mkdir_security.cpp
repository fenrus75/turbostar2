#include "../../agentlib/tool_registry.h"
#include "../../agentlib/ai_agent.h"
#include "fs_mkdir.h"

namespace tools
{

bool fs_mkdir_validator::is_allowed_in_plan_mode(const nlohmann::json& args, const agentlib::tool_context& ctx) const {
    if (!args.contains("path") || !args["path"].is_string()) return false;
    std::string path = args["path"].get<std::string>();
    if (path.starts_with("tmp://") || path.starts_with("tmp:/")) return true;
    if (!ctx.active_agent) return false;
    std::string plan_file = ctx.active_agent->get_plan_file();
    return !plan_file.empty() && path == plan_file;
}

std::unique_ptr<agentlib::llm_tool> fs_mkdir_validator::create_tool_from_resolved_path(const std::string &safe_path) const
{
	return std::make_unique<fs_mkdir_tool>(safe_path);
}

REGISTER_TOOL(fs_mkdir_validator)

} // namespace tools
