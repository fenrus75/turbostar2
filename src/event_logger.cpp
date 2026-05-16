#include "event_logger.h"
#include <algorithm>

event_logger &event_logger::get_instance()
{
	static event_logger instance;
	return instance;
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
	events.push_back(message);
	if (log_stream_.is_open()) {
		log_stream_ << message << std::endl;
	}
}

std::optional<std::string>
event_logger::get_latest_matching_message(const std::string &substring) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = events.rbegin(); it != events.rend(); ++it) {
		if (substring.empty() ||
		    it->find(substring) != std::string::npos) {
			return *it;
		}
	}
	return std::nullopt;
}
