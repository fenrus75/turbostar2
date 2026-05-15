#include "event_logger.h"
#include <algorithm>

event_logger& event_logger::get_instance()
{
	static event_logger instance;
	return instance;
}

void event_logger::log(const std::string& message)
{
	std::lock_guard<std::mutex> lock(mutex_);
	events.push_back(message);
	// We might need to handle this differently for tests, but for now, this ensures 
	// log is updated in memory immediately.
}

void event_logger::write_to_file(const std::string& filename)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::ofstream out(filename);
	if (out.is_open()) {
		for (const auto& event : events) {
			out << event << "\n";
		}
	}
}

std::optional<std::string> event_logger::get_latest_matching_message(const std::string& substring) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto it = events.rbegin(); it != events.rend(); ++it) {
		if (substring.empty() || it->find(substring) != std::string::npos) {
			return *it;
		}
	}
	return std::nullopt;
}
