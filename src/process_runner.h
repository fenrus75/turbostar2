#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "document.h"

/**
 * @brief Runs a shell command in the background and streams output to a document.
 */
class process_runner
{
      public:
	process_runner(std::shared_ptr<document> output_doc, int max_lines = 1000);
	~process_runner();

	void execute(const std::string &command);
	void stop();

	bool is_running() const { return is_running_.load(); }

      private:
	void worker_loop(std::string command);

	std::shared_ptr<document> doc_;
	int max_lines_;

	std::thread worker_thread_;
	std::atomic<bool> is_running_{false};
	std::atomic<bool> stop_requested_{false};
};
