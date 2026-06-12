#include <format>
#include "agent_compress_history.h"
#include "agentlib/ai_agent.h"
#include "agentlib/interactions/system_message.h"

namespace tools
{

agent_compress_history_tool::agent_compress_history_tool(agent_compress_history_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_system_message>(std::format("History Paged Out: {}", args_.title));
}

std::shared_ptr<agentlib::agent_interaction> agent_compress_history_tool::get_interaction() const
{
	return interaction_;
}

bool agent_compress_history_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (!ctx.active_agent->is_mutation_possible()) {
		out_error = "Execution Error: History mutation is not possible for the current agent.";
		return false;
	}
	if (ctx.active_agent->is_read_only()) {
		out_error = "Execution Error: Agent is in read-only mode.";
		return false;
	}
	if (args_.title.empty()) {
		out_error = "Execution Error: Milestone archive title cannot be empty.";
		return false;
	}
	if (args_.summary.empty()) {
		out_error = "Execution Error: Milestone archive summary cannot be empty.";
		return false;
	}
	return true;
}

std::string agent_compress_history_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context.";
	}
	try {
		ctx.active_agent->page_out_prior_context(args_.target_episode_id, args_.include_all_prior, args_.title, args_.summary,
							 args_.tags);
		return "History successfully paged out. Your context window has been cleared and replaced with a summary pointer.";
	} catch (const std::exception &e) {
		return std::format("Error while paging out history: {}", e.what());
	}
}

} // namespace tools