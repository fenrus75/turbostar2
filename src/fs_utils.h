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
	 * @brief Returns the safe, project-specific directory for storing SQLite databases.
	 * Resolves to ~/.cache/turbostar/projects/<hash>/dbs/ and creates the directory if it doesn't exist.
	 */
	std::string get_project_db_dir();
}