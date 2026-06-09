#include "event_logger.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

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

void event_logger::set_log_file(const std::string &filename)
{
	std::lock_guard<std::mutex> lock(mutex_);
	log_stream_.open(filename, std::ios::app);
}

void event_logger::log(const std::string &message)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto now = std::chrono::steady_clock::now();
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();

	std::ostringstream ss;
	ss << "[" << std::setw(6) << std::setfill('0') << ms << "ms] " << message;
	std::string formatted_message = ss.str();

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

void event_logger::enable_stdout_logging(bool enable)
{
	std::lock_guard<std::mutex> lock(mutex_);
	stdout_logging_ = enable;
	if (enable) {
		start_time_ = std::chrono::steady_clock::now();
	}
}

std::optional<std::string> event_logger::get_latest_matching_message(const std::string &substring) const
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
