#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

/**
 * RAII read-only memory-mapped file abstraction with transparent heap fallback,
 * lazy/eager zero-copy line splitting, binary detection, refcounted LRU caching,
 * and background readahead prefetching.
 *
 * Goals & Constraints:
 * - Maps regular files into the process virtual address space using `mmap(PROT_READ, MAP_PRIVATE)`
 *   so large files do not require heap allocation or eager full-file disk reads.
 * - Supports opt-in `populate` mode (`MAP_POPULATE`, `MAP_HUGETLB` fallback, `MADV_HUGEPAGE` /
 *   `MADV_WILLNEED`) for callers that scan the entire file (`fs_read_lines`, `fs_grep_files`,
 *   `fs_regexp_lines`).
 * - Closes the underlying file descriptor immediately after `mmap` succeeds so no file descriptors
 *   remain held while the mapping is active.
 * - Transparently falls back to a heap buffer (`std::vector<uint8_t>`) populated via `::read`
 *   if `mmap` is unsupported by the underlying filesystem or fails.
 * - Provides cached `is_binary()` detection over the first 4,096 bytes in memory without extra
 *   `open`/`read` syscalls, and zero-copy `lines()` (`std::span<const std::string_view>`) split
 *   via vectorized `std::memchr`.
 * - Provides a thread-safe 64-entry refcounted LRU cache (`get_cached`) validated via `stat()`
 *   (`st_dev`, `st_ino`, `st_size`, `st_mtim`), proactive invalidation + background re-warming
 *   (`notify_changed`), bounded background readahead (`prefetch`), and telemetry (`get_cache_stats`).
 * - Deletes rvalue conversion operators (`const &&`) to prevent dangling `std::span` or
 *   `std::string_view` bindings to temporary `mapped_file` objects at compile time.
 */
class mapped_file
{
      public:
	struct cache_stats {
		uint64_t hits{0};
		uint64_t misses{0};
		uint64_t stale_evictions{0};
		uint64_t notify_invalidations{0};
		uint64_t notify_rewarms{0};
		uint64_t capacity_evictions{0};
		uint64_t prefetch_enqueued{0};
		uint64_t prefetch_completed{0};
		uint64_t prefetch_hits{0};
		uint64_t unused_prefetch_evictions{0};
		size_t current_entries{0};
		size_t current_bytes{0};
	};

	static constexpr size_t k_max_cache_entries = 64;
	static constexpr size_t k_max_prefetch_window = 24;

	mapped_file() noexcept = default;
	~mapped_file() noexcept;

	mapped_file(const mapped_file &) = delete;
	mapped_file &operator=(const mapped_file &) = delete;

	mapped_file(mapped_file &&other) noexcept;
	mapped_file &operator=(mapped_file &&other) noexcept;

	// Opens and maps `path` for read-only access.
	// Validates that the opened descriptor refers to a regular file (`S_ISREG`).
	// When `populate` is true, requests page-table prefaulting (`MAP_POPULATE` / `madvise`)
	// and pre-builds the line index for non-binary files.
	bool open(/* untrusted */ std::string_view path, std::string &out_error, bool populate = false) noexcept;

	// Releases any active memory mapping or fallback buffer.
	void close() noexcept;

	[[nodiscard]] constexpr bool is_open() const noexcept { return is_open_; }
	[[nodiscard]] constexpr bool is_mapped() const noexcept { return mapped_ptr_ != nullptr; }
	[[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
	[[nodiscard]] constexpr size_t size() const noexcept { return size_; }
	[[nodiscard]] constexpr const uint8_t *data() const noexcept { return data_; }

	// Returns true if the first 4,096 bytes contain binary control characters (or NUL bytes when
	// `treat_null_as_binary` is true). Lazily computed once in-memory on first call with zero syscalls.
	[[nodiscard]] bool is_binary(bool treat_null_as_binary = true) const noexcept;

	[[nodiscard]] constexpr uint8_t operator[](size_t index) const noexcept { return data_[index]; }

	[[nodiscard]] constexpr std::span<const uint8_t> bytes() const & noexcept { return {data_, size_}; }
	[[nodiscard]] constexpr std::string_view string_view() const & noexcept
	{
		return {reinterpret_cast<const char *>(data_), size_};
	}

	// Returns a zero-copy span of line views (with trailing '\r' stripped) into the mapped buffer.
	// Thread-safe: lazily constructed once on first access if not already built during `open(..., true)`.
	[[nodiscard]] std::span<const std::string_view> lines() const &;

	constexpr operator std::span<const uint8_t>() const & noexcept { return bytes(); }
	constexpr operator std::string_view() const & noexcept { return string_view(); }

	// Returns a zero-copy string_view spanning the 1-based inclusive line range [start_line, end_line].
	[[nodiscard]] std::string_view line_range_view(size_t start_line, size_t end_line) const &;

	// Prevent binding non-owning views to a temporary mapped_file
	void bytes() const && = delete;
	void string_view() const && = delete;
	void lines() const && = delete;
	void line_range_view(size_t, size_t) const && = delete;
	operator std::span<const uint8_t>() const && = delete;
	operator std::string_view() const && = delete;
	[[nodiscard]] constexpr dev_t st_dev() const noexcept { return dev_; }
	[[nodiscard]] constexpr ino_t st_ino() const noexcept { return ino_; }
	[[nodiscard]] constexpr time_t mtime_sec() const noexcept { return mtime_sec_; }
	[[nodiscard]] constexpr long mtime_nsec() const noexcept { return mtime_nsec_; }

	// Returns a refcounted `mapped_file` from the global 64-entry LRU cache if `(dev, ino, size, mtime)`
	// match the current file on disk, or opens and caches a new instance on a miss.
	static std::shared_ptr<const mapped_file> get_cached(
	    /* untrusted */ std::string_view path, std::string &out_error, bool populate = true) noexcept;

	// Proactively invalidates a cached file when modified by an editor save or file tool.
	// If the file was hot in the cache, enqueues it at the front of the background readahead queue.
	static void notify_changed(/* untrusted */ std::string_view path) noexcept;

	// Enqueues one or more file paths into the bounded background readahead worker queue.
	// Caps queued items to prevent the readahead thread from evicting unconsumed cache entries.
	static void prefetch(/* untrusted */ std::string_view path) noexcept;
	static void prefetch(/* untrusted */ std::span<const std::string> paths) noexcept;

	// Retrieves a snapshot of cache hit/miss/eviction/prefetch telemetry.
	static cache_stats get_cache_stats() noexcept;

	// Clears all cached entries and resets cache telemetry (useful for unit tests).
	static void clear_cache() noexcept;

	// Stops and joins the background readahead worker thread during application shutdown.
	static void shutdown_cache() noexcept;

      private:
	void build_lines_locked() const;

	const uint8_t *data_{nullptr};
	void *mapped_ptr_{nullptr};
	std::vector<uint8_t> fallback_buf_;
	size_t size_{0};
	dev_t dev_{0};
	ino_t ino_{0};
	time_t mtime_sec_{0};
	long mtime_nsec_{0};
	bool is_open_{false};
	mutable std::atomic<uint8_t> binary_flags_{0};

	/*
	 * Protects lazy initialization of `lines_` and `lines_ready_`.
	 * Locking rules: Acquired only inside `lines()` when `lines_ready_` is false, or during
	 * move/close operations. Must never be held while acquiring the global cache mutex or
	 * performing blocking file I/O.
	 */
	mutable std::mutex lines_mutex_;
	mutable std::vector<std::string_view> lines_;
	mutable std::atomic<bool> lines_ready_{false};
};
