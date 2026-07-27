#include "event_logger.h"
#include <algorithm>
#include <format>
#include <iostream>

event_logger::event_logger() : start_time_(std::chrono::steady_clock::now())
{
}

event_logger &event_logger::get_instance()
{
	static event_logger *instance = new event_logger();
	return *instance;
}

event_logger::~event_logger()
{
	if (log_stream_.is_open()) {
		log_stream_.close();
	}
}

void event_logger::set_log_file(std::string_view filename)
{
	std::lock_guard<std::mutex> lock(mutex_);
	log_stream_.open(std::string(filename), std::ios::app);
}

#include <cstring>
#include <unistd.h>

void event_logger::log(std::string_view message)
{
	auto now = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();

	std::string formatted_message = std::format("[{:06d}ms] {}", ms, message);

	// Populate lock-free signal-safe ring buffer with trailing '\n' included
	uint64_t seq = ring_write_seq_.fetch_add(1, std::memory_order_relaxed);
	size_t slot_idx = seq % LOG_RING_SLOTS;
	auto &slot = ring_slots_[slot_idx];

	// Phase 1: Mark slot invalid (length = 0)
	slot.length.store(0, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_release);

	// Phase 2: Copy formatted_message into slot.data buffer and ensure trailing '\n'
	size_t msg_len = formatted_message.size();
	size_t to_copy = std::min(msg_len, LOG_RING_SLOT_SIZE - 2);
	std::memcpy(slot.data, formatted_message.data(), to_copy);
	slot.data[to_copy] = '\n';
	slot.data[to_copy + 1] = '\0';
	size_t total_written = to_copy + 1;

	// Phase 3: Publish actual written length
	slot.length.store(total_written, std::memory_order_release);

	std::lock_guard<std::mutex> lock(mutex_);

	events.push_back(formatted_message);
	total_events_logged_++;
	if (events.size() > 5000) {
		events.erase(events.begin());
	}
	if (log_stream_.is_open()) {
		log_stream_ << formatted_message << std::endl;
	}
	if (stdout_logging_ && ms >= 50) {
		std::cout << formatted_message << std::endl;
	}
}

void event_logger::dump_recent_logs_signal_safe(int fd, size_t max_count)
{
	if (fd < 0) {
		return;
	}
	auto &inst = get_instance();
	uint64_t total_logged = inst.ring_write_seq_.load(std::memory_order_acquire);
	if (total_logged == 0) {
		return;
	}

	size_t count = std::min(static_cast<size_t>(total_logged), std::min(max_count, LOG_RING_SLOTS));
	uint64_t start_seq = total_logged - count;

	const char *header = "\nRecent Debug Logs:\n";
	write(fd, header, 20);

	for (uint64_t seq = start_seq; seq < total_logged; ++seq) {
		size_t idx = seq % LOG_RING_SLOTS;
		const auto &slot = inst.ring_slots_[idx];
		size_t len = slot.length.load(std::memory_order_acquire);
		if (len > 0 && len < LOG_RING_SLOT_SIZE) {
			write(fd, slot.data, len);
		}
	}
}

void event_logger::enable_stdout_logging(bool enable)
{
	std::lock_guard<std::mutex> lock(mutex_);
	stdout_logging_ = enable;
	if (enable) {
		start_time_ = std::chrono::steady_clock::now();
	}
}

std::optional<std::string> event_logger::get_latest_matching_message(std::string_view substring) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = events.rbegin(); it != events.rend(); ++it) {
		if (substring.empty() || it->find(substring) != std::string::npos) {
			return *it;
		}
	}
	return std::nullopt;
}

uint64_t event_logger::get_total_event_count() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return total_events_logged_;
}

std::vector<std::string> event_logger::get_event_slice(uint64_t start_seq, uint64_t end_seq) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<std::string> slice;
	if (start_seq >= end_seq || events.empty()) {
		return slice;
	}

	uint64_t base_seq = 0;
	if (total_events_logged_ > events.size()) {
		base_seq = total_events_logged_ - events.size();
	}

	for (uint64_t seq = start_seq; seq < end_seq; ++seq) {
		if (seq < base_seq) {
			continue;
		}
		uint64_t idx = seq - base_seq;
		if (idx < events.size()) {
			slice.push_back(events[idx]);
		}
	}
	return slice;
}
