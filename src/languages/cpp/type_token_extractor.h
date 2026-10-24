#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "languages/source_token.h"

namespace languages::cpp
{

using candidate_type_token = languages::source_token;

/**
 * @brief Heuristic scanner to extract candidate type tokens from C and C++ code lines.
 *
 * Scans for identifiers that look like struct/class/enum types (e.g. types in variable
 * declarations, pointer/reference types, template arguments, and function parameter/return types).
 * Automatically filters out language keywords and standard container types.
 */
class type_token_extractor
{
      public:
	/**
	 * @brief Extract candidate type identifiers from a sequence of C/C++ code lines.
	 * @param lines Read-only sequence of line strings.
	 * @param start_line 1-based start line of the lines sequence in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<source_token> extract_candidates(std::span<const std::string> lines, int start_line);

	/**
	 * @brief Extract candidate type identifiers from contiguous C/C++ source text.
	 * @param source Contiguous string view into source buffer.
	 * @param start_line 1-based start line of the source text in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<source_token> extract_candidates_from_source(std::string_view source, int start_line);
};

} // namespace languages::cpp
