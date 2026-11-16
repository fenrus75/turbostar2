#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toolcall_watcher {

/**
 * @brief Represents a discovered toolcall.<index> trace file on disk.
 */
struct toolcall_entry {
	uint64_t index = 0;
	std::filesystem::path path;
};

/**
 * @brief Parsed representation of a single toolcall.<N> trace file.
 */
struct parsed_toolcall {
	std::string name;
	std::string c_style_call;
	std::string duration_label;
	std::string body_markdown;
	uint64_t total_duration_us = 0;
};

/**
 * @brief Discovers, parses, and formats toolcall.<N> trace files for live demo display.
 */
class toolcall_parser {
    public:
	/**
	 * @brief Scan a directory for files matching toolcall.<uint> sorted numerically ascending.
	 */
	[[nodiscard]] static std::vector<toolcall_entry> scan_directory(const std::filesystem::path &dir);

	/**
	 * @brief Parse a toolcall.<N> file from disk. Returns std::nullopt if unreadable or incomplete/malformed.
	 */
	[[nodiscard]] static std::optional<parsed_toolcall> parse_file(const std::filesystem::path &path);

	/**
	 * @brief Parse raw toolcall.<N> file content. Returns std::nullopt if header JSON is incomplete/malformed.
	 */
	[[nodiscard]] static std::optional<parsed_toolcall> parse_content(/* untrusted */ std::string_view content);

	/**
	 * @brief Strip matching outer <name_result> ... </name_result> XML wrapper lines if present.
	 */
	[[nodiscard]] static std::string strip_result_wrapper(/* untrusted */ std::string_view body);

	/**
	 * @brief Format a microsecond duration into a compact human-readable label (e.g. "4.86s", "102ms", "<1ms").
	 */
	[[nodiscard]] static std::string format_duration(uint64_t duration_us);

	/**
	 * @brief Replace ASCII control characters (except '\n' and '\t') with spaces to prevent terminal escape injection.
	 */
	[[nodiscard]] static std::string sanitize_for_terminal(/* untrusted */ std::string_view text);
};

} // namespace toolcall_watcher
