#pragma once
#include <atomic>
#include <cstddef>
#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

static constexpr size_t LOG_RING_SLOTS = 16;
static constexpr size_t LOG_RING_SLOT_SIZE = 256;

struct log_ring_slot {
	std::atomic<size_t> length{0};
	char data[LOG_RING_SLOT_SIZE];
};

class event_logger
{
      public:
	static event_logger &get_instance();

	void set_log_file(const std::string &filename);
	void log(const std::string &message);
	void enable_stdout_logging(bool enable);

	template <typename... Args>
	void log(std::string_view fmt, const Args&... args) {
		log(std::vformat(fmt, std::make_format_args(args...)));
	}
	std::optional<std::string> get_latest_matching_message(const std::string &substring) const;
	uint64_t get_total_event_count() const;
	std::vector<std::string> get_event_slice(uint64_t start_seq, uint64_t end_seq) const;
	static void dump_recent_logs_signal_safe(int fd, size_t max_count = 10);

      private:
	event_logger();
	~event_logger();
	event_logger(const event_logger &) = delete;
	event_logger &operator=(const event_logger &) = delete;

	std::vector<std::string> events;
	uint64_t total_events_logged_{0};
	/*
	 * mutex_ protects the events history log vector, the total_events_logged_ counter,
	 * the log_stream_ file handle, and stdout_logging_ settings.
	 * Locking Rules:
	 * - Held briefly when adding new log events, changing the log file destination,
	 *   or searching for matching log strings / slicing.
	 */
	mutable std::mutex mutex_;
	std::ofstream log_stream_;
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	bool stdout_logging_{false};

	log_ring_slot ring_slots_[LOG_RING_SLOTS];
	std::atomic<uint64_t> ring_write_seq_{0};
};
