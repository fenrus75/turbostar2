#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <optional>

class event_logger {
public:
	static event_logger& get_instance();

	void log(const std::string& message);
	void write_to_file(const std::string& filename);
	std::optional<std::string> get_latest_matching_message(const std::string& substring) const;

private:
	event_logger() = default;
	~event_logger() = default;
	event_logger(const event_logger&) = delete;
	event_logger& operator=(const event_logger&) = delete;

	std::vector<std::string> events;
	mutable std::mutex mutex_;
};
