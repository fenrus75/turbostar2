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
	 * @brief Parses compile_commands.json to find the exact compile command for a file.
	 */
	std::string get_compile_command_for_file(const std::string& filepath, const std::string& build_dir);
}