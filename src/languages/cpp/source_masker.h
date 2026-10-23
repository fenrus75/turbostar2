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
 * Replaces line and block comments and literals ("...", '...', R"(...)") with ASCII spaces (' ').
 *
 * Important offset invariant:
 * - Operates at the byte level: each byte within a comment or literal is replaced by a single ' ' byte.
 * - This guarantees that the line's byte length and the byte offsets of all active code identifiers
 *   remain 100% exact for RE2 matchers and pointer-arithmetic token extractors.
 * - Multi-byte UTF-8 notes: Multi-byte characters (e.g. multi-byte emojis or non-ASCII characters)
 *   inside comments or string literals are replaced by an equivalent number of 1-byte ASCII space bytes.
 *   This preserves exact byte positions rather than Unicode code point counts or visual terminal display widths.
 *   If Unicode character count invariance is ever required in the future, multi-byte UTF-8 characters
 *   can be replaced with multi-byte whitespace sequences of the exact same byte length.
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
