#include "compaction_engine.h"
#include "data/episode.h"
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
	return chars / 4; // Simple heuristic: 1 token ≈ 4 characters
}

std::vector<transition> compaction_engine::plan_compaction(const std::vector<active_episode_info>& active_episodes,
                                                           int current_tokens,
                                                           int target_tokens)
{
	if (current_tokens <= target_tokens) {
		return {};
	}

	std::vector<transition> planned;
	int total_tokens = current_tokens;

	// Create a copy of the active episodes so we can simulate transitions
	std::vector<active_episode_info> candidates = active_episodes;

	// Sort candidates by LRU (least recently used first)
	std::sort(candidates.begin(), candidates.end(), [](const active_episode_info& a, const active_episode_info& b) {
		return a.lru_seq < b.lru_seq;
	});

	for (auto& cand : candidates) {
		if (total_tokens <= target_tokens) {
			break;
		}

		// Progressive transition for this candidate
		if (cand.current_level == COMPACTION_LEVEL_RAW) {
			int savings = cand.tokens_level_0 - cand.tokens_level_1;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = COMPACTION_LEVEL_STRIP_REASONING;
				planned.push_back({cand.id, COMPACTION_LEVEL_STRIP_REASONING});
			}
		}

		if (total_tokens <= target_tokens) {
			break;
		}

		if (cand.current_level == COMPACTION_LEVEL_STRIP_REASONING) {
			int savings = cand.tokens_level_1 - cand.tokens_level_2;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = COMPACTION_LEVEL_STRIP_TOOL_CALLS;
				planned.push_back({cand.id, COMPACTION_LEVEL_STRIP_TOOL_CALLS});
			}
		}

		if (total_tokens <= target_tokens) {
			break;
		}

		if (cand.current_level == COMPACTION_LEVEL_STRIP_TOOL_CALLS) {
			// Estimate paged-out anchor size as ~50 tokens
			int anchor_size = 50;
			int savings = cand.tokens_level_2 - anchor_size;
			if (savings > 0) {
				total_tokens -= savings;
				cand.current_level = COMPACTION_LEVEL_PAGED_OUT;
				planned.push_back({cand.id, COMPACTION_LEVEL_PAGED_OUT});
			}
		}
	}

	return planned;
}

} // namespace agentlib
