#pragma once

#include <filesystem>

namespace fs_utils {
	/**
	 * @brief Safely returns the absolute path. If a filesystem error occurs, 
	 * it logs the error and returns the original path as a fallback.
	 */
	std::filesystem::path safe_absolute(const std::filesystem::path& p);
}