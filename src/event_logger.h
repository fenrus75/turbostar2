#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

static constexpr size_t LOG_RING_SLOTS = 16;
static constexpr size_t LOG_RING_SLOT_SIZE = 256;
static constexpr size_t MAX_RECENT_ERRORS = 16;

enum class log_level : uint8_t {
	debug = 0,
	info = 1,
	warn = 2,
	error = 3,
	fatal = 4
};

constexpr std::string_view log_level_to_string(log_level level) noexcept
{
	switch (level) {
	case log_level::debug:
		return "DEBUG";
	case log_level::info:
		return "INFO";
	case log_level::warn:
		return "WARN";
	case log_level::error:
		return "ERROR";
	case log_level::fatal:
		return "FATAL";
	}
	return "UNKNOWN";
}

struct log_ring_slot {
	std::atomic<size_t> length{0};
	char data[LOG_RING_SLOT_SIZE];
};

struct log_error_record {
	uint64_t timestamp_ms{0};
	log_level level{log_level::error};
	std::string message;
};

class event_logger
{
      public:
	static event_logger &get_instance();

	void set_log_file(std::string_view filename);
	void set_min_level(log_level level) noexcept;
	log_level get_min_level() const noexcept;

	void log(log_level level, std::string_view message);

	template <typename... Args>
	void log(log_level level, std::format_string<Args...> fmt, Args &&...args)
	{
		if (level < min_level_.load(std::memory_order_relaxed)) {
			return;
		}
		log(level, std::format(fmt, std::forward<Args>(args)...));
	}

	// Backwards-compatible convenience overloads defaulting to log_level::info
	void log(std::string_view message)
	{
		log(log_level::info, message);
	}

	template <typename... Args>
	void log(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::info, fmt, std::forward<Args>(args)...);
	}

	// Ergonomic level-specific convenience methods
	void log_debug(std::string_view message)
	{
		log(log_level::debug, message);
	}
	template <typename... Args>
	void log_debug(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::debug, fmt, std::forward<Args>(args)...);
	}

	void log_info(std::string_view message)
	{
		log(log_level::info, message);
	}
	template <typename... Args>
	void log_info(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::info, fmt, std::forward<Args>(args)...);
	}

	void log_warn(std::string_view message)
	{
		log(log_level::warn, message);
	}
	template <typename... Args>
	void log_warn(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::warn, fmt, std::forward<Args>(args)...);
	}

	void log_warning(std::string_view message)
	{
		log(log_level::warn, message);
	}
	template <typename... Args>
	void log_warning(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::warn, fmt, std::forward<Args>(args)...);
	}

	void log_error(std::string_view message)
	{
		log(log_level::error, message);
	}
	template <typename... Args>
	void log_error(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::error, fmt, std::forward<Args>(args)...);
	}

	void log_fatal(std::string_view message)
	{
		log(log_level::fatal, message);
	}
	template <typename... Args>
	void log_fatal(std::format_string<Args...> fmt, Args &&...args)
	{
		log(log_level::fatal, fmt, std::forward<Args>(args)...);
	}

	std::vector<log_error_record> get_recent_errors(size_t max_count = 5) const;
	size_t get_total_error_count() const noexcept;
	void print_exit_error_report() const;

	void enable_stdout_logging(bool enable);
	std::optional<std::string> get_latest_matching_message(std::string_view substring = "") const;
	uint64_t get_total_event_count() const;
	std::vector<std::string> get_event_slice(uint64_t start_seq, uint64_t end_seq) const;
	static void dump_recent_logs_signal_safe(int fd, size_t max_count = 10);

	void clear_all();

      private:
	event_logger();
	~event_logger();
	event_logger(const event_logger &) = delete;
	event_logger &operator=(const event_logger &) = delete;

	std::atomic<log_level> min_level_{log_level::debug};
	std::atomic<size_t> total_errors_logged_{0};

	std::vector<std::string> events;
	std::vector<log_error_record> recent_errors_;
	uint64_t total_events_logged_{0};

	/*
	 * mutex_ protects the events history log vector, the recent_errors_ vector,
	 * the total_events_logged_ counter, the log_stream_ file handle, and stdout_logging_ settings.
	 * Locking Rules:
	 * - Held briefly when adding new log events, changing the log file destination,
	 *   querying recent errors, or searching for matching log strings / slicing.
	 * - Never held across external I/O syscalls other than synchronous stream writes.
	 */
	mutable std::mutex mutex_;
	std::ofstream log_stream_;
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	bool stdout_logging_{false};

	log_ring_slot ring_slots_[LOG_RING_SLOTS];
	std::atomic<uint64_t> ring_write_seq_{0};
};