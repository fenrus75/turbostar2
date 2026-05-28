#include <sstream>
#include <algorithm>
#include <vector>
#include "../../agentlib/ai_agent.h"
#include "agent_list_episodes.h"

namespace tools
{

bool agent_list_episodes_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string agent_list_episodes_tool::execute(agentlib::tool_context &ctx)
{
	auto episodes = ctx.active_agent->get_episode_index();
	if (episodes.empty()) {
		set_success(ctx, "0 episodes");
		return "No archived episodes found.";
	}

	std::vector<const agentlib::episode_index_entry*> sorted;
	for (const auto &pair : episodes) {
		sorted.push_back(&pair.second);
	}
	std::sort(sorted.begin(), sorted.end(), [](const agentlib::episode_index_entry* a, const agentlib::episode_index_entry* b) {
		return a->episode_seq < b->episode_seq;
	});

	std::ostringstream oss;
	oss << "| Episode | When to Resume |\n";
	oss << "|---|---|\n";

	for (const auto* mi : sorted) {
		std::string hint = mi->reactivation_hint;
		if (hint.empty()) {
			hint = "(No reactivation hint available)";
		}
		oss << "| " << mi->id << " | " << hint << " |\n";
	}

	set_success(ctx, std::to_string(sorted.size()) + " episodes");
	return oss.str();
}

} // namespace tools
