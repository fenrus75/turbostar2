#pragma once

#include <filesystem>
#include <string>

namespace fs_utils {
	/**
	 * @brief Safely returns the absolute path. If a filesystem error occurs, 
	 * it logs the error and returns the original path as a fallback.
	 */
	std::filesystem::path safe_absolute(const std::filesystem::path& p);

	/**
	 * @brief Rapidly counts the number of lines in a file by scanning memory. Returns empty string if file is binary or too large.
	 */
	std::string count_lines_in_file(const std::string& filepath);

	/**
	 * @brief Parses compile_commands.json to find the exact compile command for a file.
	 */
	std::string get_compile_command_for_file(const std::string& filepath, const std::string& build_dir);

	/**
	 * @brief Executes a shell command synchronously, capturing stdout and stderr.
	 * It also parses the output lines using gcc_log_parser and populates build_error_manager.
	 */
	std::string execute_command_sync(const std::string& cmd);

	/**
	 * @brief Returns the global Turbostar cache directory (~/.cache/turbostar).
	 */
	std::string get_global_cache_dir();

	/**
	 * @brief Returns the base cache directory for the current project.
	 * Resolves to ~/.cache/turbostar/projects/<hash>/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_cache_root();

	/**
	 * @brief Returns the safe, project-specific directory for storing SQLite databases.
	 * Resolves to ~/.cache/turbostar/projects/<hash>/dbs/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_db_dir();

	/**
	 * @brief Returns a safe, project-specific directory for temporary files (avoiding /tmp which is sandboxed).
	 * Resolves to ~/.cache/turbostar/projects/<hash>/tmp/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_tmp_dir();

	/**
	 * @brief Returns a safe, project-specific directory for storing agent conversation history archives.
	 * Resolves to ~/.cache/turbostar/projects/<hash>/history/<agent_name>/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_history_dir(const std::string& agent_name = "main");
	/**
	 * @brief Returns a safe, project-specific directory for storing crash dumps.	 * Resolves to ~/.cache/turbostar/projects/<hash>/dumps/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_dump_dir();
	/**
	 * @brief Validates a database name against a strict whitelist (a-zA-Z0-9_-).
	 * Prevents directory traversal, spaces, SQL injection payloads, and file extensions.
	 */
	bool is_valid_db_name(const std::string& name);

	/**
	 * @brief Returns true if the string is safe to be interpolated into a shell command line.
	 * Enforces a strict allowlist of alphanumeric characters and specific safe punctuation
	 * to prevent shell injection (;, |, $, `, &) and directory traversal (..).
	 */
	bool is_shell_safe(const std::string& s);

	/**
	 * @brief Returns true if the string is safe for display in the UI (status line).	 * Rejects any string containing non-printable characters or ANSI escape sequences
	 * to prevent malicious agents from spoofing UI elements.
	 */
	bool is_safe_for_ui(const std::string& s);

	/**
	 * @brief Shortens a filename to fit within a given max_length.
	 * Rules:
	 * 1. Keep the basename.
	 * 2. If there's space, add the first directory (from the left).
	 * 3. For all remaining space, go backwards from the right.
	 * 4. Keep space for "...." (four dots) to indicate omission.
	 */
	std::string shorten_filename(const std::string& path, int max_length);
	}