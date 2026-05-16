#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include "event_queue.h"

enum class git_status {
	unknown,
	clean,
	dirty,
	untracked
};

/**
 * @brief Manages Git status checks asynchronously using child processes.
 */
class git_manager
{
      public:
	static git_manager &get_instance();

	void start(event_queue &queue);
	void stop();

	/**
	 * @brief Non-blocking request to update the Git status of a file.
	 */
	void request_status(const std::string &filepath);

	/**
	 * @brief Non-blocking request to git-add a file.
	 */
	void git_add(const std::string &filepath);

	/**
	 * @brief Synchronously returns the last known status of a file.
	 */
	git_status get_cached_status(const std::string &filepath) const;

      private:
	git_manager() = default;
	~git_manager();

	void worker_loop();
	git_status run_git_status_cmd(const std::string &filepath);
	void run_git_add_cmd(const std::string &filepath);

	enum class request_type { status, add };
	struct git_request {
		request_type type;
		std::string filepath;
	};

	std::thread worker_thread_;
	std::mutex queue_mutex_;
	std::condition_variable cv_;
	std::queue<git_request> pending_requests_;
	std::atomic<bool> stop_thread_{false};

	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, git_status> status_cache_;
	event_queue *global_queue_{nullptr};
};
