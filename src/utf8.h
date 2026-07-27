#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace utf8
{

/**
 * @brief Returns the expected length in bytes (1 to 4) of a UTF-8 character starting with lead_byte.
 */
constexpr size_t char_len(unsigned char lead_byte) noexcept
{
	if (lead_byte < 0x80)
		return 1;
	if ((lead_byte & 0xE0) == 0xC0)
		return 2;
	if ((lead_byte & 0xF0) == 0xE0)
		return 3;
	if ((lead_byte & 0xF8) == 0xF0)
		return 4;
	return 1; // Invalid UTF-8 sequence, treat as 1 byte to make progress
}

/**
 * @brief Returns the count of UTF-8 characters in a string.
 */
size_t length(std::string_view s) noexcept;

/**
 * @brief Returns the visual display width of a UTF-8 string on a terminal.
 */
size_t display_width(std::string_view s) noexcept;

/**
 * @brief Translates a character position to a byte offset in a string.
 * Returns s.length() if char_pos is out of bounds.
 */
size_t char_to_byte_offset(std::string_view s, size_t char_pos) noexcept;

/**
 * @brief Translates a byte offset to a character position in a string.
 * Returns the character length of s if byte_offset is >= s.length().
 */
size_t byte_to_char_pos(std::string_view s, size_t byte_offset) noexcept;

/**
 * @brief Retrieves the next UTF-8 character string and advances byte_offset.
 * @return true if a character was fetched, false if at the end of the string.
 */
bool next_character(std::string_view s, size_t &byte_offset, std::string &out_char);

/**
 * @brief Sanitizes a string by replacing invalid UTF-8 sequences with '?'.
 */
std::string sanitize(std::string_view s);

/**
 * @brief Returns true if s is a valid UTF-8 string, containing no null bytes or invalid UTF-8 sequences.
 */
bool is_valid_utf8(std::string_view s) noexcept;

/**
 * @brief Returns a trimmed copy of the string (removing leading and trailing whitespace).
 */
std::string trim(std::string_view s);

/**
 * @brief Removes trailing whitespace (spaces, tabs, newlines, carriage returns) from a string in-place.
 */
void trim_trailing_whitespace(std::string& s);

/**
 * @brief Wrap a text string to a specific display width, prepending prefix to the first line and indents subsequent lines.
 */
std::vector<std::string> wrap_string(std::string_view prefix, std::string_view text, int width);

/**
 * @brief Detect the MIME type of a buffer using libmagic.
 */
std::string detect_mime(std::string_view buffer);

/**
 * @brief Removes ANSI escape codes (CSI sequences) from a string.
 */
std::string sanitize_terminal_output(std::string_view input);

} // namespace utf8
