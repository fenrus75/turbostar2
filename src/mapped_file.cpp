#include "mapped_file.h"
#include "event_logger.h"
#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <format>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
// Maximum file size allowed when falling back from mmap() to heap allocation (256 MB)
constexpr size_t k_max_fallback_read_size = 256ULL * 1024ULL * 1024ULL;
// Maximum virtual mapping size (4 GB) to guard against runaway mappings on corrupted filesystems
constexpr size_t k_max_mmap_size = 4ULL * 1024ULL * 1024ULL * 1024ULL;
// Maximum file size for synchronous MAP_POPULATE prefaulting (64 MB)
constexpr size_t k_max_populate_size = 64ULL * 1024ULL * 1024ULL;
// Threshold at or above which hugepage mappings / MADV_HUGEPAGE are requested (2 MB PMD size)
constexpr size_t k_hugepage_threshold = 2ULL * 1024ULL * 1024ULL;
// Maximum individual file size eligible for the shared LRU cache (64 MB)
constexpr size_t k_max_cacheable_file_size = 64ULL * 1024ULL * 1024ULL;
// Maximum aggregate bytes stored across all entries in the shared LRU cache (256 MB)
constexpr size_t k_max_cache_total_bytes = 256ULL * 1024ULL * 1024ULL;
// Maximum pending paths in the readahead queue (kept <= half of k_max_cache_entries so
// readahead can never overrun the cache and evict unconsumed prefetched entries).
constexpr size_t k_max_prefetch_queue_size = mapped_file::k_max_prefetch_window;

struct cache_entry {
	std::shared_ptr<const mapped_file> file;
	dev_t dev{0};
	ino_t ino{0};
	size_t size{0};
	time_t mtime_sec{0};
	long mtime_nsec{0};
	uint64_t last_access_tick{0};
	bool from_prefetch{false};
	bool accessed_by_consumer{false};
};

std::atomic<bool> g_cache_destroyed{false};

class mapped_file_cache_manager
{
      public:
	static mapped_file_cache_manager &get_instance() noexcept
	{
		static mapped_file_cache_manager instance;
		return instance;
	}

	mapped_file_cache_manager() noexcept = default;
	~mapped_file_cache_manager() noexcept
	{
		shutdown();
		g_cache_destroyed.store(true, std::memory_order_release);
	}

	mapped_file_cache_manager(const mapped_file_cache_manager &) = delete;
	mapped_file_cache_manager &operator=(const mapped_file_cache_manager &) = delete;
	std::shared_ptr<const mapped_file> get_cached(
	    /* untrusted */ std::string_view path, std::string &out_error, bool populate) noexcept
	{
		if (path.empty()) {
			out_error = "File path is empty.";
			return nullptr;
		}
		std::string safe_path(path);

		struct stat st{};
		if (::stat(safe_path.c_str(), &st) != 0) {
			int err = errno;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				erase_entry_locked(safe_path);
			}
			out_error = std::format("Failed to stat file: {}", std::strerror(err));
			return nullptr;
		}

		if (!S_ISREG(st.st_mode)) {
			out_error = "Target path is not a regular file.";
			return nullptr;
		}

		std::shared_ptr<const mapped_file> hit_file;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = entries_.find(safe_path);
			if (it != entries_.end()) {
				cache_entry &entry = it->second;
				if (entry.dev == st.st_dev && entry.ino == st.st_ino &&
				    entry.size == static_cast<size_t>(st.st_size) &&
				    entry.mtime_sec == st.st_mtim.tv_sec &&
				    entry.mtime_nsec == st.st_mtim.tv_nsec) {
					++stats_.hits;
					if (entry.from_prefetch && !entry.accessed_by_consumer) {
						++stats_.prefetch_hits;
					}
					entry.accessed_by_consumer = true;
					entry.last_access_tick = ++access_tick_;
					hit_file = entry.file;
					maybe_log_stats_locked();
				} else {
					++stats_.stale_evictions;
					erase_entry_locked(it);
				}
			}
			if (!hit_file) {
				++stats_.misses;
				maybe_log_stats_locked();
			}
		}

		if (hit_file) {
			if (populate && !hit_file->is_binary()) {
				(void)hit_file->lines();
			}
			return hit_file;
		}

		// Cold miss: open and map outside the cache mutex to avoid blocking concurrent readers
		std::shared_ptr<mapped_file> new_file;
		try {
			new_file = std::make_shared<mapped_file>();
		} catch (...) {
			out_error = "Out of memory allocating mapped_file.";
			return nullptr;
		}

		if (!new_file->open(safe_path, out_error, populate)) {
			return nullptr;
		}

		if (new_file->size() <= k_max_cacheable_file_size) {
			std::lock_guard<std::mutex> lock(mutex_);
			insert_entry_locked(safe_path, new_file, /*from_prefetch=*/false, /*accessed=*/true);
		}

		return new_file;
	}

	void notify_changed(/* untrusted */ std::string_view path) noexcept
	{
		if (path.empty()) {
			return;
		}
		std::string safe_path(path);
		bool should_rewarm = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			++stats_.notify_invalidations;
			auto it = entries_.find(safe_path);
			if (it != entries_.end()) {
				erase_entry_locked(it);
				should_rewarm = !is_shutting_down_;
				if (should_rewarm) {
					++stats_.notify_rewarms;
					enqueue_prefetch_locked(safe_path, /*front=*/true);
				}
			}
		}
		if (should_rewarm) {
			cv_.notify_one();
		}
	}

	void prefetch(/* untrusted */ std::string_view path) noexcept
	{
		if (path.empty()) {
			return;
		}
		std::string safe_path(path);
		bool queued = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!is_shutting_down_) {
				queued = enqueue_prefetch_locked(safe_path, /*front=*/false);
			}
		}
		if (queued) {
			cv_.notify_one();
		}
	}

	void prefetch(/* untrusted */ std::span<const std::string> paths) noexcept
	{
		if (paths.empty()) {
			return;
		}
		bool any_queued = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (is_shutting_down_) {
				return;
			}
			for (const std::string &untrusted_p : paths) {
				if (prefetch_queue_.size() >= k_max_prefetch_queue_size) {
					break;
				}
				std::string safe_path(untrusted_p);
				if (!safe_path.empty() && enqueue_prefetch_locked(safe_path, /*front=*/false)) {
					any_queued = true;
				}
			}
		}
		if (any_queued) {
			cv_.notify_one();
		}
	}

	mapped_file::cache_stats get_stats() noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		mapped_file::cache_stats snap = stats_;
		snap.current_entries = entries_.size();
		snap.current_bytes = current_bytes_;
		return snap;
	}

	void clear() noexcept
	{
		std::lock_guard<std::mutex> lock(mutex_);
		entries_.clear();
		prefetch_queue_.clear();
		prefetch_queued_set_.clear();
		current_bytes_ = 0;
		access_tick_ = 0;
		stats_ = {};
	}

	void shutdown() noexcept
	{
		std::thread worker_to_join;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			is_shutting_down_ = true;
			prefetch_queue_.clear();
			prefetch_queued_set_.clear();
			cv_.notify_all();
			if (worker_thread_.joinable()) {
				worker_to_join = std::move(worker_thread_);
			}
		}
		if (worker_to_join.joinable()) {
			worker_to_join.join();
		}
	}

      private:
	void erase_entry_locked(std::unordered_map<std::string, cache_entry>::iterator it) noexcept
	{
		if (it == entries_.end()) {
			return;
		}
		if (current_bytes_ >= it->second.size) {
			current_bytes_ -= it->second.size;
		} else {
			current_bytes_ = 0;
		}
		entries_.erase(it);
	}

	void erase_entry_locked(const std::string &key) noexcept
	{
		erase_entry_locked(entries_.find(key));
	}

	void evict_if_needed_locked(size_t incoming_bytes) noexcept
	{
		while (!entries_.empty() &&
		       (entries_.size() >= mapped_file::k_max_cache_entries ||
		        current_bytes_ + incoming_bytes > k_max_cache_total_bytes)) {
			// Prefer evicting the oldest entry that has already been consumed; only evict an
			// unconsumed prefetched entry if all cached entries are unconsumed prefetches.
			auto victim_it = entries_.end();
			for (auto it = entries_.begin(); it != entries_.end(); ++it) {
				if (!it->second.accessed_by_consumer && it->second.from_prefetch) {
					continue;
				}
				if (victim_it == entries_.end() ||
				    it->second.last_access_tick < victim_it->second.last_access_tick) {
					victim_it = it;
				}
			}
			if (victim_it == entries_.end()) {
				for (auto it = entries_.begin(); it != entries_.end(); ++it) {
					if (victim_it == entries_.end() ||
					    it->second.last_access_tick < victim_it->second.last_access_tick) {
						victim_it = it;
					}
				}
			}
			if (victim_it == entries_.end()) {
				break;
			}
			if (victim_it->second.from_prefetch && !victim_it->second.accessed_by_consumer) {
				++stats_.unused_prefetch_evictions;
			}
			++stats_.capacity_evictions;
			erase_entry_locked(victim_it);
		}
	}

	void insert_entry_locked(const std::string &safe_path,
	                         const std::shared_ptr<const mapped_file> &file,
	                         bool from_prefetch,
	                         bool accessed) noexcept
	{
		auto existing = entries_.find(safe_path);
		if (existing != entries_.end()) {
			if (from_prefetch && existing->second.accessed_by_consumer) {
				// A synchronous consumer already populated this entry while prefetch was in flight.
				return;
			}
			erase_entry_locked(existing);
		}

		evict_if_needed_locked(file->size());

		try {
			cache_entry entry;
			entry.file = file;
			entry.dev = file->st_dev();
			entry.ino = file->st_ino();
			entry.size = file->size();
			entry.mtime_sec = file->mtime_sec();
			entry.mtime_nsec = file->mtime_nsec();
			entry.last_access_tick = ++access_tick_;
			entry.from_prefetch = from_prefetch;
			entry.accessed_by_consumer = accessed;

			entries_.emplace(safe_path, std::move(entry));
			current_bytes_ += file->size();
		} catch (...) {
			// Ignore OOM during cache insertion
		}
	}

	bool enqueue_prefetch_locked(const std::string &safe_path, bool front) noexcept
	{
		if (is_shutting_down_) {
			return false;
		}
		if (entries_.contains(safe_path) || prefetch_queued_set_.contains(safe_path)) {
			return false;
		}
		if (!front && prefetch_queue_.size() >= k_max_prefetch_queue_size) {
			return false;
		}
		try {
			ensure_worker_started_locked();
			if (front) {
				if (prefetch_queue_.size() >= k_max_prefetch_queue_size && !prefetch_queue_.empty()) {
					prefetch_queued_set_.erase(prefetch_queue_.back());
					prefetch_queue_.pop_back();
				}
				prefetch_queue_.push_front(safe_path);
			} else {
				prefetch_queue_.push_back(safe_path);
			}
			prefetch_queued_set_.insert(safe_path);
			++stats_.prefetch_enqueued;
			return true;
		} catch (...) {
			return false;
		}
	}

	void ensure_worker_started_locked()
	{
		if (worker_started_ || is_shutting_down_) {
			return;
		}
		worker_started_ = true;
		worker_thread_ = std::thread([this]() { worker_loop(); });
	}

	void worker_loop() noexcept
	{
		while (true) {
			std::string target_path;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this]() { return !prefetch_queue_.empty() || is_shutting_down_; });
				if (is_shutting_down_) {
					break;
				}
				target_path = std::move(prefetch_queue_.front());
				prefetch_queue_.pop_front();
				prefetch_queued_set_.erase(target_path);
			}

			struct stat st{};
			if (::stat(target_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
			    static_cast<size_t>(st.st_size) > k_max_cacheable_file_size) {
				continue;
			}

			{
				std::lock_guard<std::mutex> lock(mutex_);
				if (is_shutting_down_) {
					break;
				}
				auto it = entries_.find(target_path);
				if (it != entries_.end() && it->second.dev == st.st_dev &&
				    it->second.ino == st.st_ino &&
				    it->second.size == static_cast<size_t>(st.st_size) &&
				    it->second.mtime_sec == st.st_mtim.tv_sec &&
				    it->second.mtime_nsec == st.st_mtim.tv_nsec) {
					continue;
				}
			}

			std::shared_ptr<mapped_file> pf_file;
			try {
				pf_file = std::make_shared<mapped_file>();
			} catch (...) {
				continue;
			}

			std::string ignore_err;
			if (!pf_file->open(target_path, ignore_err, /*populate=*/true)) {
				continue;
			}

			{
				std::lock_guard<std::mutex> lock(mutex_);
				if (is_shutting_down_) {
					break;
				}
				insert_entry_locked(target_path, pf_file, /*from_prefetch=*/true, /*accessed=*/false);
				++stats_.prefetch_completed;
			}
		}
	}

	void maybe_log_stats_locked() noexcept
	{
		uint64_t total_lookups = stats_.hits + stats_.misses;
		if (total_lookups > 0 && (total_lookups % 64) == 0) {
			try {
				event_logger::get_instance().log(std::format(
				    "mapped_file_cache: lookups={} hits={} misses={} prefetch_hits={}/{} "
				    "stale_evict={} notify_inv={} rewarms={} cap_evict={} unused_pf_evict={} "
				    "entries={} bytes={}",
				    total_lookups,
				    stats_.hits,
				    stats_.misses,
				    stats_.prefetch_hits,
				    stats_.prefetch_completed,
				    stats_.stale_evictions,
				    stats_.notify_invalidations,
				    stats_.notify_rewarms,
				    stats_.capacity_evictions,
				    stats_.unused_prefetch_evictions,
				    entries_.size(),
				    current_bytes_));
			} catch (...) {
				// Never throw from cache telemetry logging
			}
		}
	}

	std::mutex mutex_;
	std::condition_variable cv_;
	std::unordered_map<std::string, cache_entry> entries_;
	std::deque<std::string> prefetch_queue_;
	std::unordered_set<std::string> prefetch_queued_set_;
	std::thread worker_thread_;
	size_t current_bytes_{0};
	uint64_t access_tick_{0};
	mapped_file::cache_stats stats_{};
	bool worker_started_{false};
	bool is_shutting_down_{false};
};

} // namespace

mapped_file::~mapped_file() noexcept
{
	close();
}

mapped_file::mapped_file(mapped_file &&other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      mapped_ptr_(std::exchange(other.mapped_ptr_, nullptr)),
      fallback_buf_(std::move(other.fallback_buf_)),
      size_(std::exchange(other.size_, 0)),
      dev_(std::exchange(other.dev_, 0)),
      ino_(std::exchange(other.ino_, 0)),
      mtime_sec_(std::exchange(other.mtime_sec_, 0)),
      mtime_nsec_(std::exchange(other.mtime_nsec_, 0)),
      is_open_(std::exchange(other.is_open_, false)),
      has_null_byte_(std::exchange(other.has_null_byte_, false)),
      has_binary_control_byte_(std::exchange(other.has_binary_control_byte_, false)),
      lines_(std::move(other.lines_)),
      lines_ready_(other.lines_ready_.exchange(false, std::memory_order_acq_rel))
{
	if (mapped_ptr_ == nullptr && !fallback_buf_.empty()) {
		data_ = fallback_buf_.data();
	}
}

mapped_file &mapped_file::operator=(mapped_file &&other) noexcept
{
	if (this != &other) {
		close();
		data_ = std::exchange(other.data_, nullptr);
		mapped_ptr_ = std::exchange(other.mapped_ptr_, nullptr);
		fallback_buf_ = std::move(other.fallback_buf_);
		size_ = std::exchange(other.size_, 0);
		dev_ = std::exchange(other.dev_, 0);
		ino_ = std::exchange(other.ino_, 0);
		mtime_sec_ = std::exchange(other.mtime_sec_, 0);
		mtime_nsec_ = std::exchange(other.mtime_nsec_, 0);
		is_open_ = std::exchange(other.is_open_, false);
		has_null_byte_ = std::exchange(other.has_null_byte_, false);
		has_binary_control_byte_ = std::exchange(other.has_binary_control_byte_, false);
		lines_ = std::move(other.lines_);
		lines_ready_.store(other.lines_ready_.exchange(false, std::memory_order_acq_rel),
		                   std::memory_order_release);
		if (mapped_ptr_ == nullptr && !fallback_buf_.empty()) {
			data_ = fallback_buf_.data();
		}
	}
	return *this;
}

bool mapped_file::open(/* untrusted */ std::string_view path, std::string &out_error, bool populate) noexcept
{
	close();

	if (path.empty()) {
		out_error = "File path is empty.";
		return false;
	}

	std::string safe_path(path);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
	int fd = ::open(safe_path.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		out_error = std::format("Failed to open file: {}", std::strerror(errno));
		return false;
	}

	struct stat st{};
	if (::fstat(fd, &st) != 0) {
		int err = errno;
		::close(fd);
		out_error = std::format("Failed to stat file: {}", std::strerror(err));
		return false;
	}

	if (!S_ISREG(st.st_mode)) {
		::close(fd);
		out_error = "Target path is not a regular file.";
		return false;
	}

	if (st.st_size < 0) {
		::close(fd);
		out_error = "File has invalid negative size.";
		return false;
	}

	dev_ = st.st_dev;
	ino_ = st.st_ino;
	mtime_sec_ = st.st_mtim.tv_sec;
	mtime_nsec_ = st.st_mtim.tv_nsec;

	const auto file_size = static_cast<size_t>(st.st_size);
	if (file_size == 0) {
		::close(fd);
		data_ = nullptr;
		size_ = 0;
		is_open_ = true;
		lines_ready_.store(true, std::memory_order_release);
		return true;
	}

	if (file_size > k_max_mmap_size) {
		::close(fd);
		out_error = "File is too large (>4GB) to map.";
		return false;
	}

	int base_flags = MAP_PRIVATE;
	if (populate && file_size <= k_max_populate_size) {
#ifdef MAP_POPULATE
		base_flags |= MAP_POPULATE;
#endif
	}

	void *mapped = MAP_FAILED;
#ifdef MAP_HUGETLB
	if (file_size >= k_hugepage_threshold) {
		mapped = ::mmap(nullptr, file_size, PROT_READ, base_flags | MAP_HUGETLB, fd, 0);
	}
#endif
	if (mapped == MAP_FAILED) {
		mapped = ::mmap(nullptr, file_size, PROT_READ, base_flags, fd, 0);
	}
	if (mapped == MAP_FAILED && base_flags != MAP_PRIVATE) {
		mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
	}

	if (mapped != MAP_FAILED) {
		::close(fd);
#ifdef MADV_HUGEPAGE
		if (file_size >= k_hugepage_threshold) {
			(void)::madvise(mapped, file_size, MADV_HUGEPAGE);
		}
#endif
#ifdef MADV_WILLNEED
		if (populate && file_size <= k_max_populate_size) {
			(void)::madvise(mapped, file_size, MADV_WILLNEED);
		}
#endif
		mapped_ptr_ = mapped;
		data_ = static_cast<const uint8_t *>(mapped);
		size_ = file_size;
		is_open_ = true;
	} else {
		// Transparent fallback: if mmap fails (e.g. FUSE/virtual filesystem or vm.max_map_count limit),
		// allocate a heap buffer and read the file contents via ::read().
		if (file_size > k_max_fallback_read_size) {
			int err = errno;
			::close(fd);
			out_error = std::format(
			    "mmap failed ({}) and file exceeds 256MB fallback read limit.", std::strerror(err));
			return false;
		}

		try {
			fallback_buf_.resize(file_size);
		} catch (...) {
			::close(fd);
			out_error = "Out of memory allocating fallback file buffer.";
			return false;
		}

		size_t total_read = 0;
		while (total_read < file_size) {
			ssize_t n = ::read(fd, fallback_buf_.data() + total_read, file_size - total_read);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				int err = errno;
				::close(fd);
				fallback_buf_.clear();
				fallback_buf_.shrink_to_fit();
				out_error = std::format("Failed to read file contents: {}", std::strerror(err));
				return false;
			}
			if (n == 0) {
				break;
			}
			total_read += static_cast<size_t>(n);
		}
		::close(fd);

		fallback_buf_.resize(total_read);
		data_ = fallback_buf_.data();
		size_ = total_read;
		is_open_ = true;
	}

	// Inspect the first 4,096 bytes in memory to classify binary vs. text (matching fs_basics.cpp)
	const size_t check_len = std::min<size_t>(size_, 4096);
	for (size_t i = 0; i < check_len; ++i) {
		uint8_t b = data_[i];
		if (b == 0) {
			has_null_byte_ = true;
		} else if ((b < 32 && b != 9 && b != 10 && b != 11 && b != 12 && b != 13 && b != 27) || b == 127) {
			has_binary_control_byte_ = true;
		}
	}

	if (populate && !is_binary(/*treat_null_as_binary=*/true)) {
		try {
			build_lines_locked();
			lines_ready_.store(true, std::memory_order_release);
		} catch (...) {
			lines_.clear();
		}
	}

	return true;
}

void mapped_file::close() noexcept
{
	if (mapped_ptr_ != nullptr) {
		(void)::munmap(mapped_ptr_, size_);
		mapped_ptr_ = nullptr;
	}
	fallback_buf_.clear();
	fallback_buf_.shrink_to_fit();
	lines_.clear();
	lines_.shrink_to_fit();
	lines_ready_.store(false, std::memory_order_release);
	data_ = nullptr;
	size_ = 0;
	dev_ = 0;
	ino_ = 0;
	mtime_sec_ = 0;
	mtime_nsec_ = 0;
	is_open_ = false;
	has_null_byte_ = false;
	has_binary_control_byte_ = false;
}

void mapped_file::build_lines_locked() const
{
	lines_.clear();
	if (data_ == nullptr || size_ == 0) {
		return;
	}

	// Reserve an initial estimate (~48 bytes per line) to minimize vector reallocations
	lines_.reserve(std::max<size_t>(16, size_ / 48));

	const char *p = reinterpret_cast<const char *>(data_);
	const char *end = p + size_;

	while (p < end) {
		const char *nl = static_cast<const char *>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
		const char *line_end = (nl != nullptr) ? nl : end;
		size_t len = static_cast<size_t>(line_end - p);
		if (len > 0 && p[len - 1] == '\r') {
			--len;
		}
		lines_.emplace_back(p, len);
		if (nl == nullptr) {
			break;
		}
		p = nl + 1;
	}
}

std::span<const std::string_view> mapped_file::lines() const &
{
	if (!lines_ready_.load(std::memory_order_acquire)) {
		std::lock_guard<std::mutex> lock(lines_mutex_);
		if (!lines_ready_.load(std::memory_order_relaxed)) {
			build_lines_locked();
			lines_ready_.store(true, std::memory_order_release);
		}
	}
	return lines_;
}

std::shared_ptr<const mapped_file> mapped_file::get_cached(
    /* untrusted */ std::string_view path, std::string &out_error, bool populate) noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		try {
			auto fallback = std::make_shared<mapped_file>();
			if (fallback->open(path, out_error, populate)) {
				return fallback;
			}
		} catch (...) {
			out_error = "Out of memory allocating mapped_file.";
		}
		return nullptr;
	}
	return mapped_file_cache_manager::get_instance().get_cached(path, out_error, populate);
}

void mapped_file::notify_changed(/* untrusted */ std::string_view path) noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return;
	}
	mapped_file_cache_manager::get_instance().notify_changed(path);
}

void mapped_file::prefetch(/* untrusted */ std::string_view path) noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return;
	}
	mapped_file_cache_manager::get_instance().prefetch(path);
}

void mapped_file::prefetch(/* untrusted */ std::span<const std::string> paths) noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return;
	}
	mapped_file_cache_manager::get_instance().prefetch(paths);
}

mapped_file::cache_stats mapped_file::get_cache_stats() noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return {};
	}
	return mapped_file_cache_manager::get_instance().get_stats();
}

void mapped_file::clear_cache() noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return;
	}
	mapped_file_cache_manager::get_instance().clear();
}

void mapped_file::shutdown_cache() noexcept
{
	if (g_cache_destroyed.load(std::memory_order_acquire)) {
		return;
	}
	mapped_file_cache_manager::get_instance().shutdown();
}
