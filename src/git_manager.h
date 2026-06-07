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

struct git_info {
	git_status status{git_status::unknown};
	std::string branch;
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
	git_info get_cached_info(const std::string &filepath) const;

	/**
	 * @brief Synchronously returns the absolute path to the git repository root.
	 * If the current directory is not in a git repository, returns an empty string.
	 */
	std::string get_repository_root() const;

	/**
	 * @brief Validates if a string is a safe and valid Git branch name.
	 */
	static bool is_valid_branch_name(const std::string &name);

	/**
	 * @brief Validates if a string is a safe Git revision expression (e.g. branch, tag, HEAD~1, HEAD^).
	 */
	static bool is_valid_revision(const std::string &revision);

      private:
	git_manager() = default;
	~git_manager();

	void worker_loop();
	git_info run_git_status_cmd(const std::string &filepath);
	void run_git_add_cmd(const std::string &filepath);

	enum class request_type { status, add };
	struct git_request {
		request_type type;
		std::string filepath;
	};

	std::thread worker_thread_;

	/*
	 * queue_mutex_ protects the pending_requests_ queue of git requests (status/add).
	 * Locking Rules:
	 * - Held briefly when queuing new requests or popping them inside the worker thread.
	 * - Used in conjunction with cv_ to wake up the worker thread.
	 */
	std::mutex queue_mutex_;
	std::condition_variable cv_;
	std::queue<git_request> pending_requests_;
	std::atomic<bool> stop_thread_{false};

	/*
	 * cache_mutex_ protects the status_cache_ map of file paths to cached git statuses.
	 * Locking Rules:
	 * - Held briefly when retrieving cached info or updating it after a git status command completes.
	 */
	mutable std::mutex cache_mutex_;
	std::unordered_map<std::string, git_info> status_cache_;
	std::atomic<event_queue*> global_queue_{nullptr};
};
