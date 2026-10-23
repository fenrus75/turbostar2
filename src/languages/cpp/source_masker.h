#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace languages::cpp
{

struct mask_state {
	bool in_block_comment{false};
	bool in_raw_string{false};
	std::string raw_string_delim;
};

/**
 * @brief Masks comments, string literals, and character literals in C/C++ source lines.
 *
 * Replaces line and block comments and literals ("...", '...', R"(...)") with spaces,
 * preserving exact line lengths, newline structure, and column offsets.
 */
class source_masker
{
      public:
	/**
	 * @brief Masks comments and literals in a single line, updating multi-line state.
	 * @param line Read-only string view of the line.
	 * @param state In-out tracker for multi-line block comments and raw strings.
	 * @return Masked line string of identical length.
	 */
	static std::string mask_line(std::string_view line, mask_state &state);

	/**
	 * @brief Masks a sequence of lines, preserving line count and line lengths.
	 * @param lines Read-only sequence of lines.
	 * @return Vector of masked lines.
	 */
	static std::vector<std::string> mask_lines(std::span<const std::string> lines);
};

} // namespace languages::cpp
