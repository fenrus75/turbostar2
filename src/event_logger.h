#pragma once
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <chrono>

class event_logger
{
      public:
	static event_logger &get_instance();

	void set_log_file(const std::string &filename);
	void log(const std::string &message);
	std::optional<std::string> get_latest_matching_message(const std::string &substring) const;

      private:
	event_logger();
	~event_logger();
	event_logger(const event_logger &) = delete;
	event_logger &operator=(const event_logger &) = delete;

	std::vector<std::string> events;
	mutable std::mutex mutex_;
	std::ofstream log_stream_;
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
};
