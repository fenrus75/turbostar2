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
 * Important offset & character invariants:
 * - Replaces characters inside comments and literals with valid UTF-8 whitespace of the exact same byte length:
 *   - 1-byte: ASCII space (' ', 0x20)
 *   - 2-byte: U+00A0 No-Break Space (\xC2\xA0)
 *   - 3-byte: U+2000 En Quad (\xE2\x80\x80)
 *   - 4-byte: U+F0020 Plane 15 Private Use Area whitespace (\xF3\xB0\x80\xA0)
 * - This guarantees both exact byte-for-byte length and 1:1 character count invariance.
 * - Byte offsets and character positions of all active code identifiers remain 100% stable for RE2 matchers,
 *   pointer-arithmetic token extractors, and utf8::byte_to_char_pos conversions.
 * - Every masked position is recognized as whitespace by utf8::is_space_at_byte.
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
