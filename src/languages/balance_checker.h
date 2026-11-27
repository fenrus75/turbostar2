#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace languages
{

struct balance_entry {
	std::string label;       // Human-readable delimiter pair name, e.g. "braces", "begin/end"
	std::string close_token; // Closing delimiter shown in warnings, e.g. "}", "end"
	int net_balance{0};      // >0: missing closing delimiters; <0: extra closing delimiters
};

/**
 * @brief Language-dispatched syntactic block/brace balance checker.
 *
 * Provides a shared generic curly-brace balance helper for C-like languages and
 * delegates to language-specific checkers under src/languages/<lang>/ when available.
 */
class balance_checker {
public:
	// Generic C-like curly-brace balance (+1 for '{', -1 for '}') skipping //, /* */, "...", and '...'.
	static int calculate_brace_balance(std::span<const std::string> lines, int start_line_0, int end_line_0);

	// Language-dispatched balance calculation for lines[start_line_0..end_line_0].
	static std::vector<balance_entry> calculate_balance(std::string_view safe_path, std::span<const std::string> lines,
							    int start_line_0, int end_line_0);

	// Formats whole-file imbalance warnings for any tracked pair that transitioned from balanced (0) to unbalanced.
	static std::string check_file_warnings(std::string_view safe_path, std::span<const std::string> before_lines,
					       std::span<const std::string> after_lines);

	// Returns true if safe_path has a recognized code/brace extension suitable for standalone whole-file balance checking
	// (avoids false positives on prose files like .md/.txt during fs_write_file).
	static bool supports_standalone_check(std::string_view safe_path);

	// Formats whole-file imbalance warnings for a newly written or overwritten file (no prior state).
	static std::string check_content_warnings(std::string_view safe_path, std::span<const std::string> lines);
	static std::string check_content_warnings(std::string_view safe_path, std::string_view content);

	// Returns true when safe_path uses the default C-like brace checker rather than a specialized language checker.
	static bool uses_generic_brace_check(std::string_view safe_path, std::span<const std::string> lines);
};

} // namespace languages
