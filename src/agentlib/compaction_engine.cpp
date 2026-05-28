#include "compaction_engine.h"
#include <algorithm>

namespace agentlib {

size_t compaction_engine::estimate_message_tokens(const message& msg)
{
	size_t chars = msg.content.length();
	if (msg.reasoning_content) {
		chars += msg.reasoning_content->length();
	}
	if (msg.role == "assistant" && msg.tool_calls) {
		for (const auto& tc : *msg.tool_calls) {
			chars += tc.function.arguments.length();
		}
	}
	return chars / 4;
}

std::vector<transition> compaction_engine::plan_compaction(const std::vector<active_episode_info>& active_episodes,
                                                           int current_tokens,
                                                           int target_tokens)
{
	std::vector<transition> planned;
	if (current_tokens <= target_tokens) {
		return planned;
	}

	// Create a copy of the active episodes so we can simulate transitions
	std::vector<active_episode_info> candidates = active_episodes;

	// Sort candidates by lru_seq ascending (oldest first)
	std::sort(candidates.begin(), candidates.end(), [](const active_episode_info& a, const active_episode_info& b) {
		return a.lru_seq < b.lru_seq;
	});

	int total_tokens = current_tokens;
	for (auto& cand : candidates) {
		if (total_tokens <= target_tokens) {
			break;
		}

		// Progressive transition for this candidate
		if (cand.current_level == 0) {
			int savings = cand.tokens_level_0 - cand.tokens_level_1;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = 1;
				planned.push_back({cand.id, 1});
			}
		}

		if (total_tokens <= target_tokens) {
			break;
		}

		if (cand.current_level == 1) {
			int savings = cand.tokens_level_1 - cand.tokens_level_2;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = 2;
				planned.push_back({cand.id, 2});
			}
		}

		if (total_tokens <= target_tokens) {
			break;
		}

		if (cand.current_level == 2) {
			// Estimate paged-out anchor size as ~50 tokens
			int anchor_size = 50;
			int savings = cand.tokens_level_2 - anchor_size;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = 99;
				planned.push_back({cand.id, 99});
			}
		}
	}

	return planned;
}

} // namespace agentlib
