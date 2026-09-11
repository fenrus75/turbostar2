#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tools
{

enum class type_cache_state {
	pending,    // LSP query dispatched / in-flight
	resolved,   // Found in project workspace with exact file & line bounds
	unresolved, // External library, STL, system header, or not found in workspace
	failed	    // LSP query timed out or errored
};

struct type_definition_entry {
	std::string type_name;
	type_cache_state state = type_cache_state::pending;
	std::string safe_file_path; // Relative project workspace path, e.g. "src/agentlib/tool_context.h"
	std::string kind;	    // "struct", "class", "enum"
	int start_line{0};
	int end_line{0};
	std::chrono::steady_clock::time_point requested_at;
};

/**
 * @brief Thread-safe opportunistic cache for project struct/class/enum definitions.
 *
 * Provides non-blocking lookups and background resolution via LSP definition queries
 * to annotate `fs_read_lines` without stalling tool execution. Pre-warms automatically
 * whenever codemaps or headers are parsed.
 */
class type_definition_cache
{
      public:
	static type_definition_cache &get_instance();

	/**
	 * @brief Look up a type in the cache without blocking.
	 * Returns std::nullopt if the type is not yet requested or tracked.
	 */
	std::optional<type_definition_entry> lookup(std::string_view type_name);

	/**
	 * @brief Check whether a type is already in the cache (in any state).
	 */
	bool contains(std::string_view type_name);

	/**
	 * @brief Direct fast registration of a known type definition.
	 * Used for zero-cost pre-warming from document symbols and companion headers.
	 */
	void register_resolved_type(std::string_view type_name, std::string_view kind, std::string_view safe_path, int start_line,
				    int end_line);

	/**
	 * @brief Request async resolution of a type definition via LSP.
	 * Inserts a 'pending' stub immediately to prevent redundant requests.
	 * Runs the LSP definition lookup asynchronously in a detached background worker.
	 */
	void request_async(std::string_view type_name, const std::string &referencing_file, int line, int character);

	/**
	 * @brief Format a list of resolved type definitions into a Markdown table.
	 */
	static std::string format_type_definition_table(const std::vector<type_definition_entry> &types);

	/**
	 * @brief Invalidate all cached types originating from a modified file.
	 */
	void invalidate_file(const std::string &safe_path);

	/**
	 * @brief Clear all cached entries (for tests or workspace reset).
	 */
	void clear();

      private:
	type_definition_cache() = default;
	~type_definition_cache() = default;
	type_definition_cache(const type_definition_cache &) = delete;
	type_definition_cache &operator=(const type_definition_cache &) = delete;

	/*
	 * mutex_ protects the types_ map from concurrent reads and writes
	 * across worker threads and main tool executions.
	 * Locking Rules:
	 * - Held briefly during lookups, registrations, and status updates.
	 * - Never held while calling external LSP queries or doing disk I/O.
	 */
	std::mutex mutex_;
	std::unordered_map<std::string, type_definition_entry> types_;
};

} // namespace tools
