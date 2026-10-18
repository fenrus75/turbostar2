#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "codemap_utils.h"

namespace tools
{

/**
 * @brief Thread-safe persistent cache for document codemap symbols.
 *
 * Caches structured codemap_symbol_info lists indexed by file path across
 * editor and MCP server restarts, with mtime/size validation, LRU eviction,
 * and startup stale-file refresh triggering.
 */
class codemap_cache
{
      public:
	struct cache_entry {
		std::filesystem::file_time_type mtime{};
		uint64_t file_size{0};
		int64_t last_used_ms{0}; // milliseconds since epoch (system_clock) for LRU
		std::vector<codemap_symbol_info> symbols;
	};

	static codemap_cache &get_instance();

	codemap_cache();
	~codemap_cache();

	// Non-copyable, non-movable singleton
	codemap_cache(const codemap_cache &) = delete;
	codemap_cache &operator=(const codemap_cache &) = delete;

	/**
	 * @brief Initializes the cache for the given project root, loading persisted data from disk.
	 * Checks mtimes of cached files; valid entries are kept, while stale files trigger background refreshes.
	 */
	void initialize(const std::string &project_root);

	/**
	 * @brief Persists dirty entries to disk and shuts down the cache.
	 */
	void shutdown();

	/**
	 * @brief Retrieves cached symbols for safe_path if current disk mtime/size match.
	 * Updates the entry's last_used timestamp.
	 * @param safe_path Normalized file path.
	 * @param min_lines Minimum line length filter for returned symbols.
	 * @param out Output symbol vector populated on cache hit.
	 * @return true on valid cache hit, false on miss or stale entry.
	 */
	bool get(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out);

	/**
	 * @brief Checks if a valid, un-stale cache entry exists for safe_path without modifying last_used.
	 */
	[[nodiscard]] bool has_valid_entry(const std::string &safe_path) const;

	/**
	 * @brief Inserts or updates symbols for safe_path using current disk mtime and size.
	 */
	void put(const std::string &safe_path, std::vector<codemap_symbol_info> symbols);

	/**
	 * @brief Invalidates the cache entry for safe_path.
	 */
	void invalidate(const std::string &safe_path);

	/**
	 * @brief Prunes least-recently-used entries if entry count exceeds max_entries.
	 */
	void prune_lru(size_t max_entries);

	/**
	 * @brief Saves current cache entries to disk atomically via a temporary file.
	 */
	void save_to_disk();

	/**
	 * @brief Loads cache entries from disk.
	 */
	void load_from_disk();

	/**
	 * @brief Clears all in-memory entries (for testing).
	 */
	void clear();

	/**
	 * @brief Returns total count of in-memory entries.
	 */
	[[nodiscard]] size_t size() const;

	/**
	 * @brief Sets custom cache file path (for testing).
	 */
	void set_cache_file_override(const std::string &filepath);

      private:
	std::string resolve_cache_filepath() const;
	std::string resolve_cache_filepath_locked() const;

	/*
	 * cache_mutex_ protects entries_ map of filepaths to cached symbol entries,
	 * project_root_ string, cache_file_override_ string, and dirty_ flag.
	 * Locking Rules:
	 * - Held briefly during get(), put(), invalidate(), prune_lru(), and in-memory map reads/writes.
	 * - Released BEFORE performing disk I/O (save_to_disk writes snapshot) or triggering background LSP refreshes.
	 */
	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, cache_entry> entries_;
	std::string project_root_;
	std::string cache_file_override_;
	bool dirty_{false};
	size_t max_entries_{5000};
};

} // namespace tools
