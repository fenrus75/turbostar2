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
 * @brief Returns true if the byte at byte_offset exists in s and starts a space
 * (ASCII whitespace or Unicode space character: NBSP, typographic space, CJK ideographic space, etc.).
 * Returns false if byte_offset is out of bounds, points to a non-space, or points to a UTF-8 continuation byte.
 */
bool is_space_at_byte(std::string_view s, size_t byte_offset) noexcept;

/**
 * @brief Returns true if the UTF-8 character at char_pos exists in s and is a space
 * (ASCII whitespace or Unicode space character: NBSP, typographic space, CJK ideographic space, etc.).
 * Returns false if char_pos is out of bounds or not a space.
 */
bool is_space_at_char(std::string_view s, size_t char_pos) noexcept;

/**
 * @brief Checks if a character is an ASCII alphanumeric character ([0-9a-zA-Z])
 *        or matches any character specified in extra.
 * Safely handles signed char to prevent ctype undefined behavior on non-ASCII bytes.
 */
[[nodiscard]] constexpr bool is_alnum(char c, std::string_view extra = {}) noexcept
{
	const auto uc = static_cast<unsigned char>(c);
	if ((uc >= '0' && uc <= '9') || (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z')) {
		return true;
	}
	return !extra.empty() && extra.find(c) != std::string_view::npos;
}

/**
 * @brief Converts an ASCII character to lowercase ([A-Z] -> [a-z]).
 * Non-ASCII bytes and other characters are returned unchanged.
 */
[[nodiscard]] constexpr char to_lower(char c) noexcept
{
	const auto uc = static_cast<unsigned char>(c);
	if (uc >= 'A' && uc <= 'Z') {
		return static_cast<char>(uc + ('a' - 'A'));
	}
	return c;
}

/**
 * @brief Converts an ASCII character to uppercase ([a-z] -> [A-Z]).
 * Non-ASCII bytes and other characters are returned unchanged.
 */
[[nodiscard]] constexpr char to_upper(char c) noexcept
{
	const auto uc = static_cast<unsigned char>(c);
	if (uc >= 'a' && uc <= 'z') {
		return static_cast<char>(uc - ('a' - 'A'));
	}
	return c;
}
/**
 * @brief Checks if the character starting at byte_offset in s is alphanumeric or matches extra.
 * Safely handles multi-byte UTF-8 character boundaries if extra contains non-ASCII characters.
 */
[[nodiscard]] bool is_alnum_at_byte(std::string_view s, size_t byte_offset, std::string_view extra = {}) noexcept;

/**
 * @brief Checks if the 0-indexed character at char_pos in s is alphanumeric or matches extra.
 */
[[nodiscard]] bool is_alnum_at_char(std::string_view s, size_t char_pos, std::string_view extra = {}) noexcept;

/**
 * @brief Returns true if s is non-empty and every character in s satisfies is_alnum with extra.
 * Safely handles multi-byte UTF-8 character boundaries.
 */
[[nodiscard]] bool is_alnum_only(std::string_view s, std::string_view extra = {}) noexcept;

/**
 * @brief Returns true if s is non-empty and contains at least one character that is not alphanumeric and not in extra.
 * Returns false if s is empty or consists purely of alphanumeric characters and extra.
 * Convenient wrapper for parameter/argument validation to avoid double negatives and explicit empty checks.
 */
[[nodiscard]] inline bool is_not_alnum_only(std::string_view s, std::string_view extra = {}) noexcept
{
	return !s.empty() && !is_alnum_only(s, extra);
}

/**
 * @brief Sanitizes a string by replacing invalid UTF-8 sequences with '?'.
 */
std::string sanitize(std::string_view s);

/**
 * @brief Returns true if s is a valid UTF-8 string, containing no null bytes or invalid UTF-8 sequences.
 */
bool is_valid_utf8(std::string_view s) noexcept;

/**
 * @brief Returns a subview of s with leading whitespace removed without memory allocations.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string_view trim_view_leading(std::string_view s) noexcept;

/**
 * @brief Returns a subview of s with trailing whitespace removed without memory allocations.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string_view trim_view_trailing(std::string_view s) noexcept;

/**
 * @brief Returns a subview of s with both leading and trailing whitespace removed without memory allocations.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string_view trim_view(std::string_view s) noexcept;

/**
 * @brief Returns a copy of s with leading whitespace removed.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string trim_leading_whitespace(std::string_view s);

/**
 * @brief Returns a copy of s with trailing whitespace removed.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string trim_trailing_whitespace(std::string_view s);

/**
 * @brief Returns a copy of s with both leading and trailing whitespace removed.
 * Removes both ASCII whitespace (std::isspace) and Unicode space characters.
 */
std::string trim_full(std::string_view s);

/**
 * @brief Returns a copy of s with both leading and trailing whitespace removed.
 * Equivalent to trim_full(s).
 */
std::string trim(std::string_view s);

/**
 * @brief Returns true if s is empty or contains only whitespace characters
 * (ASCII whitespace and Unicode space characters).
 */
bool is_whitespace_only(std::string_view s) noexcept;

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

/**
 * @brief Returns a copy of s with all ASCII uppercase letters ([A-Z]) converted to lowercase.
 * Multibyte UTF-8 sequences and non-ASCII bytes are preserved without modification.
 */
[[nodiscard]] std::string to_lower(std::string_view s);

/**
 * @brief Returns a copy of s with all ASCII lowercase letters ([a-z]) converted to uppercase.
 * Multibyte UTF-8 sequences and non-ASCII bytes are preserved without modification.
 */
[[nodiscard]] std::string to_upper(std::string_view s);
/**
 * @brief Converts an identifier (PascalCase, camelCase, kebab-case, or snake_case) to snake_case.
 */
std::string to_snake_case(std::string_view s);

/**
 * @brief Converts an identifier (snake_case, camelCase, kebab-case, or PascalCase) to PascalCase.
 */
std::string to_PascalCase(std::string_view s);

inline std::string to_pascal_case(std::string_view s)
{
	return to_PascalCase(s);
}

/**
 * @brief Converts an identifier (snake_case, PascalCase, kebab-case, or camelCase) to camelCase.
 */
std::string to_camelCase(std::string_view s);

inline std::string to_camel_case(std::string_view s)
{
	return to_camelCase(s);
}

enum class param_case_style {
	snake_case,  // Canonical turbostar style (e.g. start_line)
	pascal_case, // Google/Antigravity style (e.g. StartLine)
	camel_case   // Microsoft/Copilot style (e.g. startLine)
};

void set_active_param_case_style(param_case_style style) noexcept;
param_case_style get_active_param_case_style() noexcept;

/**
 * @brief Formats a canonical snake_case parameter name according to the active parameter case style.
 */
std::string format_param_name(std::string_view snake_param);

/**
 * @brief Formats a markdown table header line (e.g. "| Symbol | start_line | end_line | lines |\n")
 * by converting lowercase-starting parameter column names into the active parameter case style
 * while preserving capitalized column titles (e.g. Symbol, Type, Kind).
 */
std::string format_markdown_table_header(std::string_view header_line);

} // namespace utf8
