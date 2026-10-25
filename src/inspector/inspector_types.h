#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Contextual query passed to inspector providers when the cursor moves or settles.
 */
struct inspector_query {
	std::string file_path;
	int line{0};
	int col{0};
	std::string token;
	std::string line_text;
	uint64_t request_id{0};
};

/**
 * @brief Scored response returned by an inspector content provider.
 */
struct inspector_result {
	std::string provider_name;
	int score{0};         // 0 - 100 quality/relevance score
	std::string markdown; // Markdown Lite formatted string
	bool is_final{true};  // True if final; false if asynchronous resolution is pending
};

/**
 * @brief Standardized confidence/priority score tiers for inspector providers.
 */
namespace inspector_scores
{
constexpr int INVALID = 0;       ///< Invalid result: must never be used or displayed
constexpr int RESOLVING_HINT = 10;
constexpr int FALLBACK_TOKEN = 15;
constexpr int LSP_FALLBACK_TOKEN = 20;
constexpr int LOCAL_SYMBOLS = 40;
constexpr int GIT_BLAME = 50;
constexpr int MAN_PAGE = 80;
constexpr int LSP_RESOLVED = 90;
constexpr int DIAGNOSTIC_WARNING = 92;
constexpr int DIAGNOSTIC_ERROR = 98;
} // namespace inspector_scores
