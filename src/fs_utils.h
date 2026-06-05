#pragma once

#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace fs_utils
{
/**
 * @brief Sets an override project directory, useful for isolating tests.
 */
void set_override_project_dir(const std::string &path);

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
 * @brief Heuristically checks if a file is a binary file by scanning for a null byte in the first 4KB.
 */
bool is_binary_file(const std::string &filepath);

/**
 * @brief Rapidly counts the number of lines in a file by scanning memory. Returns empty string if file is binary or too large.
 */
std::string count_lines_in_file(const std::string &filepath);

/**
 * @brief Parses compile_commands.json to find the exact compile command for a file.
 */
std::string get_compile_command_for_file(const std::string &filepath, const std::string &build_dir);

/**
 * @brief Executes a shell command synchronously, capturing stdout and stderr.
 * It also parses the output lines using gcc_log_parser and populates build_error_manager.
 */
std::string execute_command_sync(const std::string &cmd);

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
std::string get_project_history_dir(const std::string &agent_name = "main");

/**
 * @brief Returns a safe, project-specific directory for storing crash dumps.
 * Resolves to ~/.cache/turbostar/projects/<hash>/dumps/.
 * @note Internally creates the directory if it does not exist. Callers do NOT need to call mkdir.
 */
std::string get_project_dump_dir();
/**
 * @brief Returns the absolute path to libturbocatch.so.
 */
std::string get_turbocatch_lib_path();
/**
 * @brief Validates a database name against a strict whitelist (a-zA-Z0-9_-).
 * Prevents directory traversal, spaces, SQL injection payloads, and file extensions.
 */
bool is_valid_db_name(const std::string &name);

/**
 * @brief Returns true if the string is safe to be interpolated into a shell command line.
 * Enforces a strict allowlist of alphanumeric characters and specific safe punctuation
 * to prevent shell injection (;, |, $, `, &) and directory traversal (..).
 */
bool is_shell_safe(const std::string &s, bool allow_tilde = false);
/**
 * @brief Escapes a string to make it safe for use as a shell argument by wrapping it in single quotes and escaping internal single quotes.
 */
std::string escape_shell_arg(const std::string &arg);

/**
 * @brief Unescapes backslash sequences like \\n, \\t, etc. into literal characters.
 */
std::string unescape_string(const std::string &input);
/**
 * @brief Returns true if the string is safe for display in the UI (status line).	 * Rejects any string containing non-printable
 * characters or ANSI escape sequences to prevent malicious agents from spoofing UI elements.
 */
bool is_safe_for_ui(const std::string &s);

/**
 * @brief Shortens a filename to fit within a given max_length.
 * Rules:
 * 1. Keep the basename.
 * 2. If there's space, add the first directory (from the left).
 * 3. For all remaining space, go backwards from the right.
 * 4. Keep space for "...." (four dots) to indicate omission.
 */
std::string shorten_filename(const std::string &path, int max_length);

template <typename T> auto escape_arg(const T &val)
{
	using Decayed = std::decay_t<T>;
	if constexpr (std::is_same_v<Decayed, std::filesystem::path>) {
		return escape_shell_arg(val.string());
	} else if constexpr (std::is_convertible_v<Decayed, std::string> || std::is_same_v<Decayed, std::string_view>) {
		return escape_shell_arg(std::string(val));
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

} // namespace fs_utils