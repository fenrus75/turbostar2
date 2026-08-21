#pragma once

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
 * @brief Sets an override project directory, useful for isolating tests.
 */
void set_override_project_dir(std::string_view path);

/**
 * @brief Returns any explicitly set override project directory.
 */
std::string get_override_project_dir();

/**
 * @brief Returns the active project directory, resolving any override.
 */
std::string get_project_dir();

/**
 * @brief Safely returns the absolute path. If a filesystem error occurs,
 * it logs the error and returns the original path as a fallback.
 */
std::filesystem::path safe_absolute(const std::filesystem::path &p);

/**
 * @brief Makes an absolute path relative to the active project root or working directory.
 */
std::string make_relative_to_project(std::string_view path_str, std::string_view working_dir = "");

/**
 * @brief Heuristically checks if a file is a binary file by scanning for a null byte in the first 4KB.
 */
enum class file_type_t {
    ASCII,
    MAYBE,
    BINARY
};

bool is_binary_file(std::string_view filepath);

/**
 * @brief Returns true if the path exists, can be stat'd, and is a regular file (S_ISREG).
 * Prevents hanging or unexpected behavior when attempting to read FIFOs, device nodes, or sockets.
 */
bool is_regular_file(std::string_view filepath) noexcept;

file_type_t get_file_type(std::string_view filepath);

/**
 * @brief Rapidly counts the number of lines in a file by scanning memory. Returns empty string if file is binary or too large.
 */
std::string count_lines_in_file(std::string_view filepath);

/**
 * @brief Parses compile_commands.json to find the exact compile command for a file.
 */
std::string get_compile_command_for_file(std::string_view filepath, std::string_view build_dir);

/**
 * @brief Executes a shell command synchronously, capturing stdout and stderr.
 * It also parses the output lines using gcc_log_parser and populates build_error_manager.
 */
std::string execute_command_sync(std::string_view cmd, int timeout_seconds = 60);

/**
 * @brief Returns the global Turbostar cache directory (~/.cache/turbostar).
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_global_cache_dir();

/**
 * @brief Returns the base cache directory for the current project.
 * Resolves to ~/.cache/turbostar/projects/<hash>/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_cache_root();

/**
 * @brief Returns the safe, project-specific directory for storing SQLite databases.
 * Resolves to ~/.cache/turbostar/projects/<hash>/dbs/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_db_dir();

/**
 * @brief Returns a safe, project-specific directory for temporary files (avoiding /tmp which is sandboxed).
 * Resolves to ~/.cache/turbostar/projects/<hash>/tmp/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_tmp_dir();

/**
 * @brief Returns a safe, project-specific directory for storing agent conversation history archives.
 * Resolves to ~/.cache/turbostar/projects/<hash>/history/<agent_name>/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_history_dir(std::string_view agent_name = "main");

/**
 * @brief Returns a safe, project-specific directory for storing crash dumps.
 * Resolves to ~/.cache/turbostar/projects/<hash>/dumps/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_dump_dir();
/**
 * @brief Returns a safe, project-specific directory for storing performance profile data.
 * Resolves to ~/.cache/turbostar/projects/<hash>/perf/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_perf_dir();
/**
 * @brief Returns the absolute path to libturbocatch.so.
 */
std::string get_turbocatch_lib_path();
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
 * @brief Returns true if the string is safe for display in the UI (status line).	 * Rejects any string containing non-printable
 * characters or ANSI escape sequences to prevent malicious agents from spoofing UI elements.
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

// Formatted overload of execute_command_sync
template <typename... Args> std::string execute_command_sync(std::string_view fmt, const Args &...args)
{
	return execute_command_sync(format_command(fmt, args...));
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
 * @brief Ensures all parent directories for target_path exist on disk.
 * Automatically creates any missing parent directories (Option B).
 * If a new directory is created and a sibling directory exists with a 1-character difference
 * (e.g. "model" vs "models"), appends a warning to note_or_warning_out.
 * Returns true if parent directory exists or was created successfully.
 */
bool ensure_parent_directory_exists(const std::string &target_path, std::string &note_or_warning_out);

} // namespace fs_utils