#include <algorithm>
#include <format>
#include <vector>
#include "agentlib/ai_agent.h"
#include "agent_list_episodes.h"
#include "fs_utils.h"

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
		return fs_utils::wrap_prompt_untrusted_data_tag("agent_list_episodes_result", "No archived episodes found.");
	}

	std::vector<const agentlib::episode_index_entry*> sorted;
	for (const auto &pair : episodes) {
		if (pair.second.reactivation_hint.find("Trivial or extremely brief") != std::string::npos) {
			continue;
		}
		sorted.push_back(&pair.second);
	}
	std::sort(sorted.begin(), sorted.end(), [](const agentlib::episode_index_entry* a, const agentlib::episode_index_entry* b) {
		return a->episode_seq < b->episode_seq;
	});

	if (sorted.empty()) {
		set_success(ctx, "0 episodes");
		return fs_utils::wrap_prompt_untrusted_data_tag("agent_list_episodes_result", "No archived episodes found.");
	}

	std::string res = "| Episode | When to Resume |\n|---|---|\n";
	for (const auto* mi : sorted) {
		std::string hint = mi->reactivation_hint;
		if (hint.empty()) {
			hint = "(No reactivation hint available)";
		}
		std::string safe_hint;
		for (char c : hint) {
			if (c == '|') {
				safe_hint += '-';
			} else if (c == '\n' || c == '\r') {
				safe_hint += ' ';
			} else if (static_cast<unsigned char>(c) >= 32 && c != 127) {
				safe_hint += c;
			}
		}
		if (safe_hint.length() > 200) {
			safe_hint = safe_hint.substr(0, 197) + "...";
		}
		res += std::format("| {} | {} |\n", mi->id, safe_hint);
	}

	set_success(ctx, std::format("{} episodes", sorted.size()));
	return fs_utils::wrap_prompt_untrusted_data_tag("agent_list_episodes_result", res);
}

} // namespace tools
