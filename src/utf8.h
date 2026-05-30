#pragma once

#include <string>
#include <string_view>

namespace utf8
{

/**
 * @brief Returns the expected length in bytes (1 to 4) of a UTF-8 character starting with lead_byte.
 */
size_t char_len(unsigned char lead_byte);

/**
 * @brief Returns the count of UTF-8 characters in a string.
 */
size_t length(std::string_view s);

/**
 * @brief Translates a character position to a byte offset in a string.
 * Returns s.length() if char_pos is out of bounds.
 */
size_t char_to_byte_offset(std::string_view s, size_t char_pos);

/**
 * @brief Translates a byte offset to a character position in a string.
 * Returns the character length of s if byte_offset is >= s.length().
 */
size_t byte_to_char_pos(std::string_view s, size_t byte_offset);

/**
 * @brief Retrieves the next UTF-8 character string and advances byte_offset.
 * @return true if a character was fetched, false if at the end of the string.
 */
bool next_character(std::string_view s, size_t &byte_offset, std::string &out_char);

} // namespace utf8
