#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tools
{

struct candidate_type_token {
	std::string name;
	int line{0}; // 1-based line number in file
	int col{0};  // 0-based character column offset in line
};

/**
 * @brief Heuristic scanner to extract candidate type tokens from code lines.
 *
 * Scans for identifiers that look like struct/class/enum types (e.g. types in variable
 * declarations, pointer/reference types, template arguments, and function parameter/return types).
 * Automatically filters out language keywords and standard container types.
 */
class type_token_extractor
{
      public:
	/**
	 * @brief Extract candidate type identifiers from a list of code lines.
	 * @param lines Vector of line strings.
	 * @param start_line 1-based start line of the lines vector in the file.
	 * @return List of candidate tokens with line and column positions.
	 */
	static std::vector<candidate_type_token> extract_candidates(const std::vector<std::string> &lines, int start_line);

	/**
	 * @brief Returns true if an identifier is a known keyword, primitive, or standard type.
	 */
	static bool is_denylisted(std::string_view identifier);
};

} // namespace tools
