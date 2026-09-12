#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tools
{

struct candidate_call_token {
	std::string name;
	int line{0}; // 1-based line number in file
	int col{0};  // 0-based character column offset in line
};

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
	 * @brief Extract candidate call tokens from a list of code lines.
	 * @param lines Vector of line strings.
	 * @param start_line 1-based start line of the lines vector in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<candidate_call_token> extract_candidates(const std::vector<std::string> &lines, int start_line);

	/**
	 * @brief Returns true if an identifier is a known control-flow keyword or language primitive.
	 */
	static bool is_denylisted(std::string_view identifier);
};

} // namespace tools
