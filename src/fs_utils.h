#pragma once

#include "fs_basics.h"
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
std::string system_header_to_include_uri(std::string_view path_str);

/**
 * @brief Heuristically checks if a file is a binary file by scanning the first 4KB.
 *
 * @param filepath Path to the file.
 * @param treat_null_as_binary If true, null bytes ('\0') cause the file to be treated as binary (MAYBE -> BINARY).
 *                             If false (default), null bytes alone yield MAYBE which returns false, preserving
 *                             compatibility for callers that tolerate null bytes.
 * @return true if the file is classified as binary, false otherwise.
 */
bool is_binary_file(std::string_view filepath, bool treat_null_as_binary = false);

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
std::string get_compile_command_for_file(std::string_view filepath, std::string_view build_dir, bool syntax_only = true);

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
std::string find_executable(std::string_view name);

// Formatted overload of execute_command_sync
template <typename... Args> std::string execute_command_sync(std::string_view fmt, const Args &...args)
{
	return execute_command_sync(format_command(fmt, args...));
}

/**
 * @brief Ensures all parent directories for target_path exist on disk.
 * Automatically creates any missing parent directories (Option B).
 * If a new directory is created and a sibling directory exists with a 1-character difference
 * (e.g. "model" vs "models"), appends a warning to note_or_warning_out.
 * Returns true if parent directory exists or was created successfully.
 */
bool ensure_parent_directory_exists(const std::string &target_path, std::string &note_or_warning_out);

/**
 * @brief When a requested file is not found, searches the project tree for an existing file
 * with the same basename. If multiple matches exist, prefers the candidate with the longest
 * matching path prefix (closest directory hierarchy to the attempted filename).
 * @param untrusted_filename The relative or absolute path requested.
 * @return Project-relative path of the best candidate, or empty string if none found.
 */
std::string filename_suggest_alternative(/* untrusted */ std::string_view untrusted_filename);

} // namespace fs_utils

