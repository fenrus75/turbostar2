#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "codemap_utils.h"

namespace languages::cpp
{

/**
 * @brief Heuristic fallback symbol parser for C and C++ source code lines.
 *
 * Scans lines using RE2 patterns, curly-brace stack tracking, and specifier keyword
 * inspection to detect functions, classes, structs, and enums while skipping prototypes.
 */
class symbol_extractor
{
      public:
	/**
	 * @brief Extract functions, classes, structs, and enums from C/C++ lines.
	 * @param lines Read-only sequence of document lines.
	 * @param min_lines Minimum span of lines for a symbol to be included (default 1).
	 * @param out Output symbol vector to append detected symbols into.
	 */
	static void extract_symbols(std::span<const std::string> lines, int min_lines, std::vector<tools::codemap_symbol_info> &out);

	/**
	 * @brief Checks if a word is a C/C++ function specifier or primitive return type.
	 */
	static bool is_function_specifier_word(std::string_view word);
};

} // namespace languages::cpp
