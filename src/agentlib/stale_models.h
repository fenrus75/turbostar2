#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace agentlib {

// Sorted list of known deprecated/stale LLM model IDs.
constexpr std::array<std::string_view, 19> STALE_MODELS = {
    "claude-1.0",
    "claude-2.0",
    "claude-2.1",
    "code-davinci-002",
    "gemini-1.0-pro",
    "gpt-3.5-turbo-0301",
    "gpt-3.5-turbo-0613",
    "gpt-4-0314",
    "gpt-4-0613",
    "gpt-4-32k",
    "gpt-4-32k-0314",
    "gpt-4-32k-0613",
    "test-stale-model",
    "text-davinci-002",
    "text-davinci-003",
    "text-embedding-ada-002",
    "text-similarity-babbage-001",
    "text-similarity-davinci-001",
    "v1"
};

inline bool is_stale_model(std::string_view model_id) {
    return std::binary_search(STALE_MODELS.begin(), STALE_MODELS.end(), model_id);
}

} // namespace agentlib
