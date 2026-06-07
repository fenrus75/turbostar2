#pragma once

#include <memory>
#include <mutex>
#include "../thread_annotations.h"
#include <string>
#include <thread>
#include <vector>
#include "mcp_server.h"

namespace agentlib
{

class mcp_manager
{
      public:
	static mcp_manager &get_instance();

	void discover_and_load(const std::string &project_root = "");
	std::vector<std::shared_ptr<mcp_server>> get_servers() const;
	std::shared_ptr<mcp_server> find_server(const std::string &name) const;
	void save_configs(const std::string &project_root = "");

	// Dynamic lifecycle control
	void start_async(const std::string &project_root);
	void start_active_servers();
	void stop_all_servers();
	void toggle_server(const std::string &name, bool enable);
	void toggle_tool(const std::string &server_name, const std::string &tool_name, bool enable);

	void queue_prompt_generation(const std::string &server_name, bool is_system);

      private:
	mcp_manager() = default;
	~mcp_manager();

	/*
	 * mutex_ protects the servers_ registry map and controls MCP manager operations.
	 *
	 * Locking Rules:
	 * - Held during server discovery, load, start, stop, and config saving/enablement toggling.
	 * - Internal helper calls (such as find_server_unlocked) must be used when the mutex is already
	 *   held by the calling thread to prevent deadlocks with standard std::mutex.
	 */
	mutable safe_mutex mutex_;

	std::vector<std::shared_ptr<mcp_server>> servers_ GUARDED_BY(mutex_);

	void load_servers_from_file_unlocked(const std::string &path, bool is_system) REQUIRES(mutex_);
	void prompt_worker_loop();

	std::shared_ptr<mcp_server> find_server_unlocked(const std::string &name) const REQUIRES(mutex_);
	void queue_prompt_generation_unlocked(const std::string &server_name, bool is_system) REQUIRES(mutex_);

	std::thread startup_thread_;

	/*
	 * prompt_mutex_ protects prompt_generation_queue_ and controls the lifecycle
	 * of the asynchronous prompt generation worker thread (prompt_generation_thread_).
	 *
	 * Locking Rules:
	 * - Held briefly when queuing a new prompt generation task, starting the worker
	 *   thread, and when checking or popping tasks from the queue in the worker loop.
	 * - Used in conjunction with prompt_cv_ to signal the worker thread when new tasks are available
	 *   or during shutdown.
	 * - Lock Ordering: Can be acquired while holding mutex_. To avoid deadlocks,
	 *   never acquire mutex_ while holding prompt_mutex_.
	 */
	mutable safe_mutex prompt_mutex_;

	std::vector<std::pair<std::string, bool>> prompt_generation_queue_ GUARDED_BY(prompt_mutex_);
	std::thread prompt_generation_thread_;

	std::condition_variable prompt_cv_;
	std::atomic<bool> prompt_thread_running_{false};
};

} // namespace agentlib
