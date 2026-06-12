#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/system_message.h"
#include "agent_restore_context.h"

namespace tools
{

agent_restore_context_tool::agent_restore_context_tool(agent_restore_context_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_system_message>("Context Paged In: " + args_.episode_id);
}

std::shared_ptr<agentlib::agent_interaction> agent_restore_context_tool::get_interaction() const
{
	return interaction_;
}

bool agent_restore_context_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (ctx.active_agent && !ctx.active_agent->is_mutation_possible()) {
		out_error = "Execution Error: History mutation is not possible for the current agent.";
		return false;
	}
	return true;
}

std::string agent_restore_context_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.active_agent) {
		if (ctx.active_agent->page_in_context(args_.episode_id, args_.compression_level)) {
			return "Context successfully restored. The previous conversation history has been injected into your active "
			       "memory.";
		} else {
			return "Error: Could not find or load episode archive: " + args_.episode_id;
		}
	}
	return "Error: No active agent context.";
}

} // namespace tools