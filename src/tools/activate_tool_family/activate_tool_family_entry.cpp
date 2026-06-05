#include <sstream>
#include "../../agentlib/ai_agent.h"
#include "activate_tool_family.h"

namespace tools
{

activate_tool_family_tool::activate_tool_family_tool(activate_tool_family_args args) : args_(std::move(args))
{
}

bool activate_tool_family_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true; // Name validation was already done in security phase
}

std::string activate_tool_family_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.active_agent) {
		ctx.active_agent->add_active_tool_family(args_.name);
	}

	std::stringstream ss;
	ss << "Tool family '" << args_.name << "' has been successfully activated.\n";
	ss << "All tools belonging to this family are now available in your context.";
	return ss.str();
}

} // namespace tools
