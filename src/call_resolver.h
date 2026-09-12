#pragma once

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "agentlib/tool_context.h"
#include "codemap_utils.h"
#include "lsp_manager.h"

namespace tools
{

/**
 * @brief Context-aware resolver for outgoing function and symbol calls.
 *
 * Responsibilities:
 * 1. Resolves candidate outgoing calls from call hierarchy items or lexical tokens.
 * 2. Provides context-aware disambiguation when a symbol is defined across multiple files
 *    (e.g., in the Linux kernel across generic headers and architecture-specific headers).
 * 3. Scans slices of source files to harvest candidate calls and queries definitions.
 */
class call_resolver
{
public:
	/**
	 * @brief Disambiguates between multiple candidate definition locations for a called symbol.
	 *
	 * Uses caller context:
	 * - Evaluates headers included by the caller file (#include <...>, #include "...").
	 * - Scores locations based on caller directory proximity, generic subsystem headers vs architecture ports.
	 * - Strictly rejects test and selftest sources for non-test caller files.
	 *
	 * @param locations Candidate definition locations returned by LSP/semcode.
	 * @param caller_file Normalized relative path of the caller file.
	 * @param ctx Optional tool context.
	 * @return Best matching location, or std::nullopt if irreconcilably ambiguous or invalid.
	 */
	static std::optional<lsp_backend::location_info> disambiguate_locations(
		std::span<const lsp_backend::location_info> locations,
		std::string_view caller_file,
		agentlib::tool_context *ctx = nullptr);

	/**
	 * @brief Resolves an outgoing call hierarchy item to its true definition file and bounds.
	 *
	 * @param ref Reference to populate with resolved target details.
	 * @param item Call hierarchy item (from LSP or semcode backend).
	 * @param symbols_cache Cache of parsed codemap symbols per file to avoid redundant re-parsing.
	 * @param ctx Optional tool context.
	 * @return true if successfully resolved to a valid project file symbol; false otherwise.
	 */
	static bool resolve_target(
		outgoing_call_reference &ref,
		const lsp_manager::call_hierarchy_item &item,
		std::unordered_map<std::string, std::vector<codemap_symbol_info>> &symbols_cache,
		agentlib::tool_context *ctx = nullptr);

	/**
	 * @brief Extracts outgoing calls from a source code slice and resolves their definitions.
	 *
	 * Scans the lines within [start_line, end_line] for call candidates, queries definitions,
	 * disambiguates candidates, and returns verified outgoing call references.
	 *
	 * @param safe_path Absolute or project-relative path to the file.
	 * @param start_line 1-based start line of the slice.
	 * @param end_line 1-based end line of the slice.
	 * @param doc_symbols Symbols already discovered in the caller document.
	 * @param symbols_cache Cache of parsed codemap symbols per file.
	 * @param ctx Optional tool context.
	 * @param deadline Deadline time_point after which extraction stops.
	 * @return Vector of resolved outgoing call references.
	 */
	static std::vector<outgoing_call_reference> extract_calls_from_slice(
		const std::string &safe_path,
		int start_line,
		int end_line,
		const std::vector<codemap_symbol_info> &doc_symbols,
		std::unordered_map<std::string, std::vector<codemap_symbol_info>> &symbols_cache,
		agentlib::tool_context *ctx,
		std::chrono::steady_clock::time_point deadline);

	/**
	 * @brief Parses include directives in a file to extract header names.
	 *
	 * @param filepath Path to the file.
	 * @return Set of included header filenames/paths.
	 */
	static std::unordered_set<std::string> extract_included_headers(const std::string &filepath);

	/**
	 * @brief Checks if a path is considered a project file.
	 */
	static bool is_project_file(std::string_view path, agentlib::tool_context *ctx = nullptr);

	/**
	 * @brief Checks if a path belongs to test or selftest directories.
	 */
	static bool is_test_path(std::string_view path);
};

} // namespace tools
