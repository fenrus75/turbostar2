#pragma once

#include <vector>
#include <string>
#include <map>
#include "llm_types.h"

namespace agentlib {

struct transition {
    std::string episode_id;
    int target_level;
};

struct active_episode_info {
    std::string id;
    int current_level;
    long long lru_seq;
    int tokens_level_0;
    int tokens_level_1;
    int tokens_level_2;
};

class compaction_engine {
public:
    // Estimate token count of a single message (approx chars/4)
    static size_t estimate_message_tokens(const message& msg);

    // Plans the progressive tiered compaction transitions to reach target_tokens
    static std::vector<transition> plan_compaction(const std::vector<active_episode_info>& active_episodes,
                                                   int current_tokens,
                                                   int target_tokens);
};

} // namespace agentlib
