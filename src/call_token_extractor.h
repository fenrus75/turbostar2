#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "languages/cpp/call_token_extractor.h"
#include "languages/source_token.h"

namespace tools
{

using source_token = languages::source_token;
using candidate_call_token = languages::source_token;

/**
 * @brief Heuristic scanner to extract candidate function call tokens from code lines.
 *
 * Scans for function call expressions: `ident(...)`, `obj.ident(...)`, `obj->ident(...)`,
 * or `Namespace::ident(...)`. Automatically filters out control flow keywords and language builtins.
 */
class call_token_extractor
{
public:
	/**
	 * @brief Extract candidate call tokens from a sequence of code lines.
	 * @param lines Read-only sequence of line strings.
	 * @param start_line 1-based start line of the lines sequence in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<source_token> extract_candidates(std::span<const std::string> lines, int start_line);

	/**
	 * @brief Extract candidate call tokens when the file type is supported.
	 * @param path File path used to determine language support.
	 * @param lines Read-only sequence of line strings.
	 * @param start_line 1-based start line of the lines sequence in the file.
	 * @return List of candidate tokens, or empty vector if the file language is unsupported.
	 */
	static std::vector<source_token> extract_candidates(std::string_view path, std::span<const std::string> lines, int start_line);
};

} // namespace tools
