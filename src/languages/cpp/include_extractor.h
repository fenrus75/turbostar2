#pragma once

#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace languages::cpp
{

struct include_info {
	std::string path;
	bool is_system{false}; // true for <...>, false for "..."
	int line{0};           // 1-based line number
};

/**
 * @brief Robust extractor for C and C++ #include directives.
 *
 * Utilizes languages::cpp::source_masker to ensure commented-out includes
 * (single-line, multi-line block comments, or raw string literals) are ignored.
 */
class include_extractor
{
      public:
	/**
	 * @brief Extracts all active #include directives from lines of code.
	 * @param lines Code lines to scan.
	 * @param start_line 1-based start line of the lines span.
	 * @return List of include_info descriptors in order of appearance.
	 */
	static std::vector<include_info> extract_includes(std::span<const std::string> lines, int start_line = 1);

	/**
	 * @brief Extracts all active #include directives directly from a file.
	 * @param filepath Path to the file.
	 * @param max_lines Maximum number of lines to scan (default 1000).
	 * @return List of include_info descriptors.
	 */
	static std::vector<include_info> extract_includes_from_file(const std::string &filepath, size_t max_lines = 1000);

	/**
	 * @brief Extracts unique header path strings from lines of code.
	 * @param lines Code lines to scan.
	 * @return Set of unique header paths.
	 */
	static std::unordered_set<std::string> extract_header_names(std::span<const std::string> lines);

	/**
	 * @brief Extracts unique header path strings from a file.
	 * @param filepath Path to the file.
	 * @param max_lines Maximum number of lines to scan (default 1000).
	 * @return Set of unique header paths.
	 */
	static std::unordered_set<std::string> extract_header_names_from_file(const std::string &filepath, size_t max_lines = 1000);
};

} // namespace languages::cpp
