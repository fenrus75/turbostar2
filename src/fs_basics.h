#pragma once

#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fs_utils
{

/**
 * @brief Heuristically checks if a file is a binary file by scanning for a null byte in the first 4KB.
 */
enum class file_type_t {
    ASCII,
    MAYBE,
    BINARY
};

/**
 * @brief Heuristically checks if a memory buffer is binary by scanning up to 4KB.
 *
 * @param buffer Buffer to inspect.
 * @param treat_null_as_binary If true, null bytes ('\0') cause the buffer to be treated as binary (MAYBE -> BINARY).
 *                             If false (default), null bytes alone yield MAYBE which returns false, preserving
 *                             compatibility for callers that tolerate null bytes.
 * @return true if the buffer is classified as binary, false otherwise.
 */
bool is_binary_buffer(std::string_view buffer, bool treat_null_as_binary = false);

/**
 * @brief Inspects a memory buffer and classifies it as ASCII, MAYBE (null byte found), or BINARY (control chars found).
 */
file_type_t get_buffer_file_type(std::string_view buffer);

/**
 * @brief Validates a database name against a strict whitelist (a-zA-Z0-9_-).
 * Prevents directory traversal, spaces, SQL injection payloads, and file extensions.
 */
bool is_valid_db_name(std::string_view name) noexcept;

/**
 * @brief Returns true if the string is safe to be interpolated into a shell command line.
 * Enforces a strict allowlist of alphanumeric characters and specific safe punctuation
 * to prevent shell injection (;, |, $, `, &) and directory traversal (..).
 */
bool is_shell_safe(std::string_view s, bool allow_tilde = false) noexcept;

/**
 * @brief Escapes a string to make it safe for use as a shell argument by wrapping it in single quotes and escaping internal single quotes.
 */
std::string escape_shell_arg(std::string_view arg);

/**
 * @brief Unescapes backslash sequences like \\n, \\t, etc. into literal characters.
 */
std::string unescape_string(std::string_view input);

/**
 * @brief Escapes a string to make it safe for insertion into a JSON payload according to RFC 8259.
 * Escapes double quotes, backslashes, control characters (\\b, \\f, \\n, \\r, \\t), and control bytes (0x00-0x1F).
 * @param input The raw untrusted input string to escape.
 * @param include_quotes If true, wraps the escaped output in double quotes ("..."). Defaults to false.
 */
std::string escape_json_string(std::string_view input, bool include_quotes = false);

/**
 * @brief Auto-repairs malformed or unterminated JSON string payloads (e.g. truncated tool call arguments).
 * Closes unterminated string literals and unclosed object/array braces. Returns "{}" if unrepairable.
 */
std::string repair_json_string(std::string_view untrusted_json);

/**
 * @brief Safely wraps untrusted user or LLM input within XML data tags for system and subagent prompt interpolation.
 * Sanitizes closing tag breakout attempts (e.g. </tag>) to prevent prompt injection into subordinate agents.
 * @param tag The XML tag name (e.g. "user_query", "target_path").
 * @param content The untrusted input content to wrap.
 */
std::string wrap_prompt_untrusted_data_tag(std::string_view tag, std::string_view content);

/**
 * @brief Unwraps content enclosed within an XML prompt data tag (<tag>...</tag>).
 * @param content The wrapped or raw content.
 */
std::string unwrap_prompt_untrusted_data_tag(std::string_view content);

/**
 * @brief Checks if content is already enclosed within a top-level XML prompt data tag (<tag>...</tag>).
 */
bool is_prompt_tag_wrapped(std::string_view content) noexcept;

/**
 * @brief Returns true if the string is safe for display in the UI (status line).
 * Rejects any string containing non-printable characters or ANSI escape sequences
 * to prevent malicious agents from spoofing UI elements.
 */
bool is_safe_for_ui(std::string_view s) noexcept;

/**
 * @brief Shortens a filename to fit within a given max_length.
 * Rules:
 * 1. Keep the basename.
 * 2. If there's space, add the first directory (from the left).
 * 3. For all remaining space, go backwards from the right.
 * 4. Keep space for "...." (four dots) to indicate omission.
 */
std::string shorten_filename(std::string_view path, int max_length);

template <typename T> auto escape_arg(const T &val)
{
	using Decayed = std::decay_t<T>;
	if constexpr (std::is_same_v<Decayed, std::filesystem::path>) {
		return escape_shell_arg(std::string_view(val.native()));
	} else if constexpr (std::is_convertible_v<Decayed, std::string_view>) {
		return escape_shell_arg(std::string_view(val));
	} else {
		return val;
	}
}

template <typename... Args> std::string format_command(std::string_view fmt, const Args &...args)
{
	auto escaped_args = std::make_tuple(escape_arg(args)...);
	return std::apply([&](auto &...unpacked_args) { return std::vformat(fmt, std::make_format_args(unpacked_args...)); }, escaped_args);
}

/**
 * @brief Base64 encodes the given text payload.
 */
std::string base64_encode(std::string_view text);
std::string base64_encode(std::span<const unsigned char> data);

/**
 * @brief Base64 decodes the given encoded payload.
 */
std::vector<unsigned char> base64_decode(std::string_view encoded);

/**
 * @brief Formats binary output as either a hex-spaced string or a base64 data URL.
 */
std::string format_binary_output(std::span<const unsigned char> data, std::string_view format, std::string_view mime_type);

/**
 * @brief Computes Levenshtein edit distance between two strings.
 */
size_t levenshtein_distance(std::string_view s1, std::string_view s2);

/**
 * @brief Sets the name of the current calling thread (truncated to 15 characters for Linux pthread_setname_np).
 */
void set_current_thread_name(std::string_view name);

/**
 * @brief Returns the name of the current calling thread.
 */
std::string get_current_thread_name();

} // namespace fs_utils
