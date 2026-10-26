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
	int available_lines{3};    ///< Number of usable content lines in companion window
};

/**
 * @brief Scored response returned by an inspector content provider.
 */
struct inspector_result {
	std::string provider_name;
	int score{0};              // 0 - 100 quality/relevance score
	std::string markdown;      // Markdown Lite formatted string
	std::string title_addon;   // Optional title addon (e.g. "man 3 qsort" or declaration location)
	bool is_final{true};       // True if final; false if asynchronous resolution is pending
};

/**
 * @brief Standardized confidence/priority score tiers for inspector providers.
 */
namespace inspector_scores
{
constexpr int INVALID = 0;           ///< Invalid result: must never be used or displayed
constexpr int RESOLVING_HINT = 10;
constexpr int FALLBACK_TOKEN = 15;
constexpr int LSP_FALLBACK_TOKEN = 20;
constexpr int CLASS_ENCLOSING = 34;    ///< Instant (0ms) enclosing class summary for ambient context
constexpr int LOCAL_SYMBOLS = 40;
constexpr int GIT_BLAME = 50;
constexpr int LSP_LOW_CONFIDENCE = 70; ///< LSP resolved without actual project path or to /usr/ (lower priority than man pages)
constexpr int LSP_SYSTEM_HEADER = LSP_LOW_CONFIDENCE; ///< Alias for backward compatibility
constexpr int MAN_PAGE = 80;          ///< System man page (preferred for libc / POSIX symbols)
constexpr int LSP_RESOLVED = 85;      ///< LSP resolved to project code / workspace symbols
constexpr int CLASS_ENCLOSING_MEMBER = 90; ///< Enclosing class summary with focused member declaration (beats LSP)
constexpr int CLASS_TOKEN = 91;        ///< Class summary for token/variable directly under cursor (score + 1)
constexpr int CLASS_SUMMARY = CLASS_TOKEN; ///< Backward-compatible alias
constexpr int DIAGNOSTIC_WARNING = 95;
constexpr int DIAGNOSTIC_ERROR = 98;
} // namespace inspector_scores
