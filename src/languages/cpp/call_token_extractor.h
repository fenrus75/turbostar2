#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "languages/source_token.h"

namespace languages::cpp
{

using candidate_call_token = languages::source_token;

/**
 * @brief Heuristic scanner to extract candidate function call tokens from C and C++ code lines.
 *
 * Scans for function call expressions: `ident(...)`, `obj.ident(...)`, `obj->ident(...)`,
 * or `Namespace::ident(...)`. Automatically filters out control flow keywords and language builtins.
 */
class call_token_extractor
{
      public:
	/**
	 * @brief Extract candidate call tokens from a sequence of C/C++ code lines.
	 * @param lines Read-only sequence of line strings.
	 * @param start_line 1-based start line of the lines sequence in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<source_token> extract_candidates(std::span<const std::string> lines, int start_line);

	/**
	 * @brief Extract candidate call tokens from contiguous C/C++ source text.
	 * @param source Contiguous string view into source buffer.
	 * @param start_line 1-based start line of the source text in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<source_token> extract_candidates_from_source(std::string_view source, int start_line);
};

} // namespace languages::cpp
