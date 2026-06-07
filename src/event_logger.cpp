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
