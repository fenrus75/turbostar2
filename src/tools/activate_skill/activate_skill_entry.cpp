#include "agentlib/ai_agent.h"
#include "agentlib/skill_manager.h"
#include "activate_skill.h"

namespace tools
{

activate_skill_tool::activate_skill_tool(activate_skill_args args) : args_(std::move(args))
{
}

bool activate_skill_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true; // Name validation was already done in security phase
}

std::string activate_skill_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.active_agent) {
		ctx.active_agent->add_active_skill(args_.name);
	}
	return agentlib::skill_manager::get_instance().format_skill_content(args_.name);
}

} // namespace tools
